#include "nv12_gpu_converter.hpp"

#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

constexpr const char *NV12_SHADER = R"(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, r8) uniform readonly image2D y_plane;
layout(set = 0, binding = 1, rg8) uniform readonly image2D uv_plane;
layout(set = 0, binding = 2, rgba8) uniform writeonly image2D rgba_output;

layout(push_constant, std430) uniform Params {
    int color_matrix;
    int full_range;
} params;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(rgba_output);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    float y = imageLoad(y_plane, pixel).r;
    vec2 uv = imageLoad(uv_plane, pixel / 2).rg - vec2(0.5);

    float luma = params.full_range != 0
        ? y
        : 1.16438356 * (y - 16.0 / 255.0);
    uv *= params.full_range != 0 ? 1.0 : 1.13839286;
    vec3 rgb;
    if (params.color_matrix == 1) {
        rgb = vec3(
            luma + 1.402 * uv.y,
            luma - 0.344136 * uv.x - 0.714136 * uv.y,
            luma + 1.772 * uv.x
        );
    } else if (params.color_matrix == 3) {
        rgb = vec3(
            luma + 1.4746 * uv.y,
            luma - 0.164553 * uv.x - 0.571353 * uv.y,
            luma + 1.8814 * uv.x
        );
    } else {
        rgb = vec3(
            luma + 1.5748 * uv.y,
            luma - 0.187324 * uv.x - 0.468124 * uv.y,
            luma + 1.8556 * uv.x
        );
    }
    imageStore(rgba_output, pixel, vec4(clamp(rgb, 0.0, 1.0), 1.0));
}
)";

RID create_texture(RenderingDevice *p_rd, RenderingDevice::DataFormat p_format,
                   int p_width, int p_height,
                   BitField<RenderingDevice::TextureUsageBits> p_usage) {
    Ref<RDTextureFormat> format;
    format.instantiate();
    format->set_format(p_format);
    format->set_width((uint32_t)p_width);
    format->set_height((uint32_t)p_height);
    format->set_depth(1);
    format->set_array_layers(1);
    format->set_mipmaps(1);
    format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
    format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
    format->set_usage_bits(p_usage);

    Ref<RDTextureView> view;
    view.instantiate();
    return p_rd->texture_create(format, view);
}

Ref<RDUniform> image_uniform(int p_binding, const RID &p_texture) {
    Ref<RDUniform> uniform;
    uniform.instantiate();
    uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
    uniform->set_binding(p_binding);
    uniform->add_id(p_texture);
    return uniform;
}

} // namespace

NV12GPUConverter::NV12GPUConverter() {
    RenderingServer *server = RenderingServer::get_singleton();
    _rd = server ? server->get_rendering_device() : nullptr;
    if (_rd && _initialize_shader()) {
        _texture.instantiate();
    } else {
        _rd = nullptr;
    }
}

NV12GPUConverter::~NV12GPUConverter() {
    reset();
    if (_rd) {
        if (_pipeline.is_valid()) {
            _rd->free_rid(_pipeline);
        }
        if (_shader.is_valid()) {
            _rd->free_rid(_shader);
        }
    }
}

bool NV12GPUConverter::is_available() const {
    return _rd && _shader.is_valid() && _pipeline.is_valid() && _texture.is_valid();
}

bool NV12GPUConverter::_initialize_shader() {
    Ref<RDShaderSource> source;
    source.instantiate();
    source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
    source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, NV12_SHADER);

    Ref<RDShaderSPIRV> spirv = _rd->shader_compile_spirv_from_source(source);
    if (spirv.is_null()) {
        UtilityFunctions::printerr("[avbridge] NV12 shader compilation returned no SPIR-V");
        return false;
    }

    String error = spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
    if (!error.is_empty()) {
        UtilityFunctions::printerr("[avbridge] NV12 shader compilation failed: ", error);
        return false;
    }

    _shader = _rd->shader_create_from_spirv(spirv, "godot-avbridge NV12");
    if (!_shader.is_valid()) {
        UtilityFunctions::printerr("[avbridge] failed to create NV12 shader");
        return false;
    }

    _pipeline = _rd->compute_pipeline_create(_shader);
    if (!_pipeline.is_valid()) {
        UtilityFunctions::printerr("[avbridge] failed to create NV12 compute pipeline");
        _rd->free_rid(_shader);
        _shader = RID();
        return false;
    }
    return true;
}

bool NV12GPUConverter::_create_textures(int p_width, int p_height) {
    _free_textures();

    const auto input_usage = (BitField<RenderingDevice::TextureUsageBits>)(
        RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
        RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
    const auto output_usage = (BitField<RenderingDevice::TextureUsageBits>)(
        RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
        RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
        RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);

    _y_texture = create_texture(_rd, RenderingDevice::DATA_FORMAT_R8_UNORM,
                                p_width, p_height, input_usage);
    _uv_texture = create_texture(_rd, RenderingDevice::DATA_FORMAT_R8G8_UNORM,
                                 p_width / 2, p_height / 2, input_usage);
    _output_texture = create_texture(_rd, RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
                                     p_width, p_height, output_usage);
    if (!_y_texture.is_valid() || !_uv_texture.is_valid() ||
        !_output_texture.is_valid()) {
        UtilityFunctions::printerr("[avbridge] failed to create NV12 GPU textures");
        _free_textures();
        return false;
    }

    TypedArray<RDUniform> uniforms;
    uniforms.push_back(image_uniform(0, _y_texture));
    uniforms.push_back(image_uniform(1, _uv_texture));
    uniforms.push_back(image_uniform(2, _output_texture));
    _uniform_set = _rd->uniform_set_create(uniforms, _shader, 0);
    if (!_uniform_set.is_valid()) {
        UtilityFunctions::printerr("[avbridge] failed to create NV12 shader uniforms");
        _free_textures();
        return false;
    }

    _width = p_width;
    _height = p_height;
    _texture->set_texture_rd_rid(_output_texture);
    return true;
}

bool NV12GPUConverter::convert(const PackedByteArray &p_y, const PackedByteArray &p_uv,
                               int p_width, int p_height,
                               avb_color_range p_range,
                               avb_color_matrix p_matrix) {
    if (!is_available() || p_width <= 0 || p_height <= 0 ||
        (p_width & 1) != 0 || (p_height & 1) != 0 ||
        p_y.size() != p_width * p_height ||
        p_uv.size() != p_width * p_height / 2) {
        return false;
    }
    if ((_width != p_width || _height != p_height) &&
        !_create_textures(p_width, p_height)) {
        return false;
    }

    if (_rd->texture_update(_y_texture, 0, p_y) != OK ||
        _rd->texture_update(_uv_texture, 0, p_uv) != OK) {
        UtilityFunctions::printerr("[avbridge] failed to upload NV12 frame");
        return false;
    }

    int64_t list = _rd->compute_list_begin();
    _rd->compute_list_bind_compute_pipeline(list, _pipeline);
    _rd->compute_list_bind_uniform_set(list, _uniform_set, 0);
    struct {
        int32_t matrix;
        int32_t full_range;

    } params = {
        (int32_t)p_matrix,
        p_range == AVB_COLOR_RANGE_FULL ? 1 : 0,

    };
    PackedByteArray push_constant;
    push_constant.resize(sizeof(params));
    memcpy(push_constant.ptrw(), &params, sizeof(params));
    _rd->compute_list_set_push_constant(
        list, push_constant, (uint32_t)push_constant.size());
    _rd->compute_list_dispatch(list,
                               (uint32_t)((p_width + 7) / 8),
                               (uint32_t)((p_height + 7) / 8), 1);
    _rd->compute_list_end();
    return true;
}

Ref<Texture2D> NV12GPUConverter::get_texture() const {
    return _texture;
}

void NV12GPUConverter::_free_textures() {
    if (!_rd) {
        return;
    }
    if (_texture.is_valid()) {
        _texture->set_texture_rd_rid(RID());
    }
    if (_uniform_set.is_valid()) {
        _rd->free_rid(_uniform_set);
        _uniform_set = RID();
    }
    if (_output_texture.is_valid()) {
        _rd->free_rid(_output_texture);
        _output_texture = RID();
    }
    if (_uv_texture.is_valid()) {
        _rd->free_rid(_uv_texture);
        _uv_texture = RID();
    }
    if (_y_texture.is_valid()) {
        _rd->free_rid(_y_texture);
        _y_texture = RID();
    }
    _width = 0;
    _height = 0;
}

void NV12GPUConverter::reset() {
    _free_textures();
}

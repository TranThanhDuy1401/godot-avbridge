#pragma once

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <avbridge.h>

namespace godot {

class NV12GPUConverter {
public:
    NV12GPUConverter();
    ~NV12GPUConverter();

    bool is_available() const;
    bool convert(const PackedByteArray &p_y, const PackedByteArray &p_uv,
                 int p_width, int p_height,
                 avb_color_range p_range, avb_color_matrix p_matrix);
    Ref<Texture2D> get_texture() const;
    void reset();

private:
    RenderingDevice *_rd = nullptr;
    Ref<Texture2DRD> _texture;

    RID _shader;
    RID _pipeline;
    RID _y_texture;
    RID _uv_texture;
    RID _output_texture;
    RID _uniform_set;

    int _width = 0;
    int _height = 0;

    bool _initialize_shader();
    bool _create_textures(int p_width, int p_height);
    void _free_textures();
};

} // namespace godot

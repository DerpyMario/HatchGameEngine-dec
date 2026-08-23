#if INTERFACE
#include <Engine/Rendering/GL/GLShader.h>
#include <Engine/Rendering/GL/Includes.h>
#include <Engine/Rendering/GL/ShaderIncludes.h>

class GLShaderBuilder {
public:

};
#endif

#include <Engine/Rendering/GL/GLShaderBuilder.h>

PRIVATE STATIC void GLShaderBuilder::AddUniformsToShaderText(std::string& shaderText, GLShaderUniforms uniforms) {
    if (uniforms.u_matrix) {
        shaderText += "uniform mat4 u_projectionMatrix;\n";
        shaderText += "uniform mat4 u_modelViewMatrix;\n";
    }
    if (uniforms.u_color) {
        shaderText += "uniform vec4 u_color;\n";
    }
    if (uniforms.u_texture) {
        shaderText += "uniform sampler2D u_texture;\n";
    }
    if (uniforms.u_palette) {
        shaderText += "uniform sampler2D u_paletteTexture;\n";
    }
    if (uniforms.u_yuv) {
        shaderText += "uniform sampler2D u_texture;\n";
        shaderText += "uniform sampler2D u_textureU;\n";
        shaderText += "uniform sampler2D u_textureV;\n";
    }
    if (uniforms.u_fog_exp || uniforms.u_fog_linear) {
        shaderText += "uniform vec4 u_fogColor;\n";

        // See BuildFogTable below: on ES the curve is worked out in the shader
        // from the smoothness rather than looked up in a table.
#ifdef GL_ES
        shaderText += "uniform float u_fogSmoothness;\n";
#else
        shaderText += "uniform float u_fogTable[256];\n";
#endif
    }
    if (uniforms.u_fog_linear) {
        shaderText += "uniform float u_fogLinearStart;\n";
        shaderText += "uniform float u_fogLinearEnd;\n";
    }
    if (uniforms.u_fog_exp) {
        shaderText += "uniform float u_fogDensity;\n";
    }
}
PRIVATE STATIC void GLShaderBuilder::AddInputsToVertexShaderText(std::string& shaderText, GLShaderLinkage inputs) {
    if (inputs.link_position) {
        shaderText += "attribute vec3 i_position;\n";
    }
    if (inputs.link_uv) {
        shaderText += "attribute vec2 i_uv;\n";
    }
    if (inputs.link_color) {
        shaderText += "attribute vec4 i_color;\n";
    }
}
PRIVATE STATIC void GLShaderBuilder::AddOutputsToVertexShaderText(std::string& shaderText, GLShaderLinkage outputs) {
    if (outputs.link_position) {
        shaderText += "varying vec4 o_position;\n";
    }
    if (outputs.link_uv) {
        shaderText += "varying vec2 o_uv;\n";
    }
    if (outputs.link_color) {
        shaderText += "varying vec4 o_color;\n";
    }
}
PRIVATE STATIC void GLShaderBuilder::AddInputsToFragmentShaderText(std::string& shaderText, GLShaderLinkage& inputs) {
    AddOutputsToVertexShaderText(shaderText, inputs);
}
// The fog table GL_BuildFogTable fills in is a quantisation curve: a pure
// function of its index and the smoothness. Desktop GLSL reads it out of a
// uniform array, but GLSL ES will not index an array with anything that is not
// a constant expression, and the index here is the depth of the fragment. So on
// ES the same curve is worked out inline instead, and has to be kept in step
// with GL_BuildFogTable.
//
// The two agree except where they cannot. GL_BuildFogTable walks the curve by
// adding the step up 256 times, and a float sum drifts a little below the exact
// multiple; where that drift crosses a whole number, floor() lands a step lower
// than the closed form here does. Swept across smoothness in ten-thousandths,
// that is 26 of 2,560,256 (index, smoothness) pairs, each off by one step of a
// 256-step gradient. No bias applied here removes them -- accumulated rounding
// is not something a closed form reproduces -- and of the two it is the running
// sum that is off, so this is left exact.
PRIVATE STATIC string GLShaderBuilder::BuildFogTableLookup() {
#ifdef GL_ES
    return
    "float fogTableAt(float index) {\n"
    "    float value = clamp(1.0 - u_fogSmoothness, 0.0, 1.0);\n"
    "    if (value <= 0.0)\n"
    "        return index / 255.0;\n"
    "    float fog = index / (254.0 * value);\n"
    "    return clamp(floor(floor(fog) * value * 256.0) / 256.0, 0.0, 1.0);\n"
    "}\n";
#else
    return
    "float fogTableAt(float index) {\n"
    "    return u_fogTable[int(index)];\n"
    "}\n";
#endif
}
PRIVATE STATIC string GLShaderBuilder::BuildFragmentShaderMainFunc(GLShaderLinkage& inputs, GLShaderUniforms& uniforms) {
    std::string shaderText = "";

    if (uniforms.u_fog_linear) {
        shaderText += GLShaderBuilder::BuildFogTableLookup();
        shaderText += "float doFogCalc(float coord, float start, float end) {\n"
        "    float invZ = 1.0 / (coord / 192.0);\n"
        "    float fogValue = (end - (1.0 - invZ)) / (end - start);\n"
        "    float result = floor(clamp(fogValue * 255.0, 0.0, 255.0));\n"
        "    return 1.0 - clamp(fogTableAt(result), 0.0, 1.0);\n"
        "}\n";
    }
    else if (uniforms.u_fog_exp) {
        shaderText += GLShaderBuilder::BuildFogTableLookup();
        shaderText += "float doFogCalc(float coord, float density) {\n"
        "    float fogValue = exp(-density * (coord / 192.0));\n"
        "    float result = floor(clamp(fogValue * 255.0, 0.0, 255.0));\n"
        "    return 1.0 - clamp(fogTableAt(result), 0.0, 1.0);\n"
        "}\n";
    }

    shaderText += "void main() {\n";
    shaderText += "vec4 finalColor;\n";

    if (uniforms.u_texture) {
        if (inputs.link_color) {
            shaderText += "if (o_color.a == 0.0) discard;\n";
            shaderText += "vec4 base = texture2D(u_texture, o_uv);\n";
            if (uniforms.u_palette) {
                shaderText += "if (base.r == 0.0) discard;\n";
                shaderText += "base = texture2D(u_paletteTexture, vec2(base.r, 0.0));\n";
            }
            else {
                shaderText += "if (base.a == 0.0) discard;\n";
            }
            shaderText += "finalColor = base * o_color;\n";
        }
        else {
            shaderText += "vec4 base = texture2D(u_texture, o_uv);\n";
            if (uniforms.u_palette) {
                shaderText += "if (base.r == 0.0) discard;\n";
                shaderText += "base = texture2D(u_paletteTexture, vec2(base.r, 0.0));\n";
            }
            else {
                shaderText += "if (base.a == 0.0) discard;\n";
            }
            shaderText += "finalColor = base * u_color;\n";
        }
    }
    else {
        if (inputs.link_color) {
            shaderText += "if (o_color.a == 0.0) discard;\n";
            shaderText += "finalColor = o_color;\n";
        }
        else {
            shaderText += "finalColor = u_color;\n";
        }
    }

    if (uniforms.u_fog_linear || uniforms.u_fog_exp) {
        shaderText += "finalColor = mix(finalColor, u_fogColor, doFogCalc(abs(o_position.z / o_position.w), ";
        if (uniforms.u_fog_linear) {
            shaderText += "u_fogLinearStart, u_fogLinearEnd";
        }
        else if (uniforms.u_fog_exp) {
            shaderText += "u_fogDensity";
        }
        shaderText += "));\n";
    }

    shaderText += "gl_FragColor = finalColor;\n";
    shaderText += "}";

    return shaderText;
}

PUBLIC STATIC string GLShaderBuilder::Vertex(GLShaderLinkage& inputs, GLShaderLinkage& outputs, GLShaderUniforms& uniforms) {
    std::string shaderText = "";

    AddInputsToVertexShaderText(shaderText, inputs);
    AddOutputsToVertexShaderText(shaderText, outputs);
    AddUniformsToShaderText(shaderText, uniforms);

    shaderText += "void main() {\n";
    shaderText += "gl_Position = u_projectionMatrix * u_modelViewMatrix * vec4(i_position, 1.0);\n";
    if (outputs.link_position) {
        shaderText += "o_position = u_modelViewMatrix * vec4(i_position, 1.0);\n";
    }
    if (outputs.link_color) {
        shaderText += "o_color = i_color;\n";
    }
    if (outputs.link_uv) {
        shaderText += "o_uv = i_uv;\n";
    }
    shaderText += "}";

    return shaderText;
}
PUBLIC STATIC string GLShaderBuilder::Fragment(GLShaderLinkage& inputs, GLShaderUniforms& uniforms, std::string mainText) {
    std::string shaderText = "";

    // GLSL ES has no default precision for float in a fragment shader, so one
    // has to be stated or nothing compiles. The vertex stage has a default and
    // does not need it.
#ifdef GL_ES
    shaderText += "precision mediump float;\n";
#endif

    AddInputsToFragmentShaderText(shaderText, inputs);
    AddUniformsToShaderText(shaderText, uniforms);

    shaderText += mainText;

    return shaderText;
}
PUBLIC STATIC string GLShaderBuilder::Fragment(GLShaderLinkage& inputs, GLShaderUniforms& uniforms) {
    return Fragment(inputs, uniforms, BuildFragmentShaderMainFunc(inputs, uniforms));
}

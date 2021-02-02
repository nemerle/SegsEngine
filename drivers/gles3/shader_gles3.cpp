/*************************************************************************/
/*  shader_gles3.cpp                                                     */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "shader_gles3.h"

#include "core/print_string.h"
#include "core/external_profiler.h"

//#define DEBUG_OPENGL

#ifdef DEBUG_OPENGL

#define DEBUG_TEST_ERROR(m_section)                                         \
    {                                                                       \
        uint32_t err = glGetError();                                        \
        if (err) {                                                          \
            print_line("OpenGL Error #" + itos(err) + " at: " + m_section); \
        }                                                                   \
    }
#else

#define DEBUG_TEST_ERROR(m_section)

#endif

ShaderGLES3 *ShaderGLES3::active = nullptr;

//#define DEBUG_SHADER

#ifdef DEBUG_SHADER

#define DEBUG_PRINT(m_text) print_line(m_text);

#else

#define DEBUG_PRINT(m_text)

#endif

void ShaderGLES3::bind_uniforms() {

    if (!uniforms_dirty) {
        return;
    }

    // upload default uniforms
    for(const auto & val : uniform_defaults) {
        int idx = val.first;
        int location = version->uniform_location[idx];

        if (location < 0) {
            continue;
        }

        const Variant &v(val.second);
        _set_uniform_variant(location, v);
        //print_line("uniform "+itos(location)+" value "+v+ " type "+Variant::get_type_name(v.get_type()));
    }

    for(const auto &c : uniform_cameras) {

        int location = version->uniform_location[c.first];
        if (location < 0) {
            continue;
        }

        glUniformMatrix4fv(location, 1, false, &(c.second.matrix[0][0]));
    }

    uniforms_dirty = false;
}

GLint ShaderGLES3::get_uniform_location(int p_index) const {

    ERR_FAIL_COND_V(!version, -1);

    return version->uniform_location[p_index];
}

bool ShaderGLES3::bind() {
    SCOPE_AUTONAMED;

    if (active != this || !version || new_conditional_version.key != conditional_version.key) {
        conditional_version = new_conditional_version;
        version = get_current_version();
    } else {

        return false;
    }

    ERR_FAIL_COND_V(!version, false);

    if (!version->ok) { //broken, unable to bind (do not throw error, you saw it before already when it failed compilation).
        glUseProgram(0);
        return false;
    }

    glUseProgram(version->id);

    DEBUG_TEST_ERROR("Use Program");

    active = this;
    uniforms_dirty = true;
    return true;
}

void ShaderGLES3::unbind() {

    version = nullptr;
    glUseProgram(0);
    uniforms_dirty = true;
    active = nullptr;
}
template<class CONTAINER>
static void _display_error_with_code(const String &p_error, const CONTAINER &p_code) {

    int line = 1;
    String total_code = String::joined(p_code,"");
    Vector<StringView> lines;
    String::split_ref(lines,total_code,'\n');

    for (size_t j = 0; j < lines.size(); j++) {

        print_line((::to_string(line) + ": " + lines[j]).c_str());
        line++;
    }

    ERR_PRINT(p_error.c_str());
}

ShaderGLES3::Version *ShaderGLES3::get_current_version() {

    auto v_iter = version_map.find(conditional_version);

    Version *_v = v_iter!=version_map.end() ? &v_iter->second : nullptr;

    if (_v) {

        if (conditional_version.code_version != 0) {
            auto cc = custom_code_map.find(conditional_version.code_version);
            ERR_FAIL_COND_V(cc== custom_code_map.end(), _v);
            if (cc->second.version == _v->code_version)
                return _v;
        } else {
            return _v;
        }
    }

    if (!_v)
        version_map[conditional_version] = Version();

    Version &v = version_map[conditional_version];

    if (!_v) {

        v.uniform_location = memnew_arr(GLint, uniform_count);

    } else {
        if (v.ok) {
            //bye bye shaders
            glDeleteShader(v.vert_id);
            glDeleteShader(v.frag_id);
            glDeleteProgram(v.id);
            v.id = 0;
        }
    }

    v.ok = false;
    /* SETUP CONDITIONALS */

    FixedVector<const char *,128,true> vs_parts;
    vs_parts.emplace_back("#version 330\n");

    for (auto & custom_define : custom_defines) {

        vs_parts.emplace_back(custom_define.data());
        vs_parts.emplace_back("\n");
    }

    for (int j = 0; j < conditional_count; j++) {

        bool enable = ((1 << j) & conditional_version.version);
        vs_parts.emplace_back(enable ? conditional_defines[j] : "");

        if (enable) {
            DEBUG_PRINT(conditional_defines[j]);
        }
    }

    //keep them around during the function
    StringView code_string;
    StringView code_string2;
    StringView code_globals;
    StringView material_string;

    CustomCode *cc = nullptr;

    if (conditional_version.code_version > 0) {
        //do custom code related stuff

        ERR_FAIL_COND_V(!custom_code_map.contains(conditional_version.code_version), nullptr);
        cc = &custom_code_map[conditional_version.code_version];
        v.code_version = cc->version;
    }

    /* CREATE PROGRAM */

    v.id = glCreateProgram();
#ifdef DEBUG_ENABLED
    char namebuf[250]={0};
    int len = snprintf(namebuf,249,"%s_%d_%d",this->get_shader_name(),conditional_version.code_version,conditional_version.version);
    if(len>0 && len<250)
        glObjectLabel(GL_PROGRAM,v.id,len,namebuf);
#endif
    ERR_FAIL_COND_V(v.id == 0, nullptr);

    /* VERTEX SHADER */

    if (cc) {
        for (int i = 0; i < cc->custom_defines.size(); i++) {

            vs_parts.emplace_back(cc->custom_defines[i].data());
            DEBUG_PRINT("CD #" + itos(i) + ": " + String(cc->custom_defines[i]));
        }
    }

    int strings_base_size = vs_parts.size();

    //vertex precision is high
    vs_parts.emplace_back("precision highp float;\n");
    vs_parts.emplace_back("precision highp int;\n");

    vs_parts.emplace_back(vertex_code_before_mats.data());

    if (cc) {
        material_string = cc->uniforms;
        vs_parts.emplace_back(material_string.data());
    }

    vs_parts.emplace_back(vertex_code_before_globals.data());

    if (cc) {
        code_globals = cc->vertex_globals;
        vs_parts.emplace_back(code_globals.data());
    }

    vs_parts.emplace_back(vertex_code_before_custom.data());

    if (cc) {
        code_string = cc->vertex;
        vs_parts.emplace_back(code_string.data());
    }

    vs_parts.emplace_back(vertex_code_after_custom.data());
#ifdef DEBUG_SHADER

    DEBUG_PRINT("\nVertex Code:\n\n" + String(code_string.data()));
    for (int i = 0; i < strings.size(); i++) {

        //print_line("vert strings "+itos(i)+":"+String(strings[i]));
    }
#endif

    v.vert_id = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v.vert_id, vs_parts.size(), vs_parts.data(), nullptr);
    glCompileShader(v.vert_id);
#ifdef DEBUG_ENABLED
    namebuf[0]=0;
    len = snprintf(namebuf,249,"%s_VS_%d_%d",this->get_shader_name(),conditional_version.code_version,conditional_version.version);
    if(len>0 && len<250)
        glObjectLabel(GL_SHADER,v.vert_id,len,namebuf);
#endif
    GLint status;

    glGetShaderiv(v.vert_id, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        // error compiling
        GLsizei iloglen;
        glGetShaderiv(v.vert_id, GL_INFO_LOG_LENGTH, &iloglen);

        if (iloglen < 0) {

            glDeleteShader(v.vert_id);
            glDeleteProgram(v.id);
            v.id = 0;

            ERR_PRINT("Vertex shader compilation failed with empty log");
        } else {

            if (iloglen == 0) {

                iloglen = 4096; //buggy driver (Adreno 220+....)
            }

            char *ilogmem = (char *)memalloc(iloglen + 1);
            ilogmem[iloglen] = 0;
            glGetShaderInfoLog(v.vert_id, iloglen, &iloglen, ilogmem);

            String err_string = String(get_shader_name()) + ": Vertex Program Compilation Failed:\n";

            err_string += ilogmem;
            _display_error_with_code(err_string, vs_parts);
            memfree(ilogmem);
            glDeleteShader(v.vert_id);
            glDeleteProgram(v.id);
            v.id = 0;
        }

        ERR_FAIL_V(nullptr);
    }

    //_display_error_with_code("pepo", strings);

    /* FRAGMENT SHADER */

    vs_parts.resize(strings_base_size);
    //fragment precision is medium
    vs_parts.emplace_back("precision highp float;\n");
    vs_parts.emplace_back("precision highp int;\n");

    vs_parts.emplace_back(fragment_code0.data());
    if (cc) {
        material_string = cc->uniforms;
        vs_parts.emplace_back(material_string.data());
    }

    vs_parts.emplace_back(fragment_code1.data());

    if (cc) {
        code_globals = cc->fragment_globals;
        vs_parts.emplace_back(code_globals.data());
    }

    vs_parts.emplace_back(fragment_code2.data());

    if (cc) {
        code_string = cc->light;
        vs_parts.emplace_back(code_string.data());
    }

    vs_parts.emplace_back(fragment_code3.data());

    if (cc) {
        code_string2 = cc->fragment;
        vs_parts.emplace_back(code_string2.data());
    }

    vs_parts.emplace_back(fragment_code4.data());

#ifdef DEBUG_SHADER
    DEBUG_PRINT("\nFragment Globals:\n\n" + String(code_globals.data()));
    DEBUG_PRINT("\nFragment Code:\n\n" + String(code_string2.data()));
    for (int i = 0; i < strings.size(); i++) {

        //print_line("frag strings "+itos(i)+":"+String(strings[i]));
    }
#endif

    v.frag_id = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v.frag_id, vs_parts.size(), &vs_parts[0], nullptr);
    glCompileShader(v.frag_id);
#ifdef DEBUG_ENABLED
    namebuf[0]=0;
    len = snprintf(namebuf,249,"%s_FS_%d_%d",this->get_shader_name(),conditional_version.code_version,conditional_version.version);
    if(len>0 && len<250)
        glObjectLabel(GL_SHADER,v.frag_id,len,namebuf);
#endif

    glGetShaderiv(v.frag_id, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        // error compiling
        GLsizei iloglen;
        glGetShaderiv(v.frag_id, GL_INFO_LOG_LENGTH, &iloglen);

        if (iloglen < 0) {

            glDeleteShader(v.frag_id);
            glDeleteShader(v.vert_id);
            glDeleteProgram(v.id);
            v.id = 0;
            ERR_PRINT("Fragment shader compilation failed with empty log");
        } else {

            if (iloglen == 0) {

                iloglen = 4096; //buggy driver (Adreno 220+....)
            }

            char *ilogmem = (char *)memalloc(iloglen + 1);
            ilogmem[iloglen] = 0;
            glGetShaderInfoLog(v.frag_id, iloglen, &iloglen, ilogmem);

            String err_string = String(get_shader_name()) + ": Fragment Program Compilation Failed:\n";

            err_string += ilogmem;
            _display_error_with_code(err_string, vs_parts);
            ERR_PRINT(err_string.c_str());
            memfree(ilogmem);
            glDeleteShader(v.frag_id);
            glDeleteShader(v.vert_id);
            glDeleteProgram(v.id);
            v.id = 0;
        }

        ERR_FAIL_V(nullptr);
    }

    glAttachShader(v.id, v.frag_id);
    glAttachShader(v.id, v.vert_id);

    // bind attributes before linking
    for (int i = 0; i < attribute_pair_count; i++) {

        glBindAttribLocation(v.id, attribute_pairs[i].index, attribute_pairs[i].name);
    }

    //if feedback exists, set it up

    if (feedback_count) {
        Vector<const char *> feedback;
        feedback.reserve(feedback_count);
        for (int i = 0; i < feedback_count; i++) {

            if (feedbacks[i].conditional == -1 || (1 << feedbacks[i].conditional) & conditional_version.version) {
                //conditional for this feedback is enabled
                feedback.emplace_back(feedbacks[i].name);
            }
        }

        if (!feedback.empty()) {
            glTransformFeedbackVaryings(v.id, feedback.size(), feedback.data(), GL_INTERLEAVED_ATTRIBS);
        }
    }

    glLinkProgram(v.id);

    glGetProgramiv(v.id, GL_LINK_STATUS, &status);

    if (status == GL_FALSE) {
        // error linking
        GLsizei iloglen;
        glGetProgramiv(v.id, GL_INFO_LOG_LENGTH, &iloglen);

        if (iloglen < 0) {

            glDeleteShader(v.frag_id);
            glDeleteShader(v.vert_id);
            glDeleteProgram(v.id);
            v.id = 0;
            ERR_FAIL_COND_V(iloglen < 0, nullptr);
        }

        if (iloglen == 0) {

            iloglen = 4096; //buggy driver (Adreno 220+....)
        }

        char *ilogmem = (char *)Memory::alloc_static(iloglen + 1);
        ilogmem[iloglen] = 0;
        glGetProgramInfoLog(v.id, iloglen, &iloglen, ilogmem);

        String err_string = String(get_shader_name()) + ": Program LINK FAILED:\n";

        err_string += ilogmem;
        _display_error_with_code(err_string, vs_parts);
        ERR_PRINT(err_string.c_str());
        Memory::free_static(ilogmem);
        glDeleteShader(v.frag_id);
        glDeleteShader(v.vert_id);
        glDeleteProgram(v.id);
        v.id = 0;

        ERR_FAIL_V(nullptr);
    }

    /* UNIFORMS */

    glUseProgram(v.id);

    //print_line("uniforms:  ");
    for (int j = 0; j < uniform_count; j++) {

        v.uniform_location[j] = glGetUniformLocation(v.id, uniform_names[j]);
        //print_line("uniform "+String(uniform_names[j])+" location "+itos(v.uniform_location[j]));
    }

    // set texture uniforms
    for (int i = 0; i < texunit_pair_count; i++) {

        GLint loc = glGetUniformLocation(v.id, texunit_pairs[i].name);
        if (loc >= 0) {
            if (texunit_pairs[i].index < 0) {
                glUniform1i(loc, max_image_units + texunit_pairs[i].index); //negative, goes down
            } else {

                glUniform1i(loc, texunit_pairs[i].index);
            }
        }
    }

    // assign uniform block bind points
    for (int i = 0; i < ubo_count; i++) {

        GLint loc = glGetUniformBlockIndex(v.id, ubo_pairs[i].name);
        if (loc >= 0)
            glUniformBlockBinding(v.id, loc, ubo_pairs[i].index);
    }

    if (cc) {

        v.texture_uniform_locations.resize(cc->texture_uniforms.size());
        for (size_t i = 0; i < cc->texture_uniforms.size(); i++) {

            v.texture_uniform_locations[i] = glGetUniformLocation(v.id, cc->texture_uniforms[i].asCString());
            glUniform1i(v.texture_uniform_locations[i], i + base_material_tex_index);
        }
    }

    glUseProgram(0);

    v.ok = true;
    if (cc) {
        cc->versions.insert(conditional_version.version);
    }

    return &v;
}

GLint ShaderGLES3::get_uniform_location(StringView p_name) const {

    ERR_FAIL_COND_V(!version, -1);
    return glGetUniformLocation(version->id, p_name.data());
}

void ShaderGLES3::setup(const char **p_conditional_defines, int p_conditional_count, const char **p_uniform_names, int p_uniform_count, const AttributePair *p_attribute_pairs, int p_attribute_count, const TexUnitPair *p_texunit_pairs, int p_texunit_pair_count, const UBOPair *p_ubo_pairs, int p_ubo_pair_count, const Feedback *p_feedback, int p_feedback_count, const char *p_vertex_code, const char *p_fragment_code, int p_vertex_code_start, int p_fragment_code_start) {

    ERR_FAIL_COND(version);
    conditional_version.key = 0;
    new_conditional_version.key = 0;
    uniform_count = p_uniform_count;
    conditional_count = p_conditional_count;
    conditional_defines = p_conditional_defines;
    uniform_names = p_uniform_names;
    vertex_code = p_vertex_code;
    fragment_code = p_fragment_code;
    texunit_pairs = p_texunit_pairs;
    texunit_pair_count = p_texunit_pair_count;
    vertex_code_start = p_vertex_code_start;
    fragment_code_start = p_fragment_code_start;
    attribute_pairs = p_attribute_pairs;
    attribute_pair_count = p_attribute_count;
    ubo_pairs = p_ubo_pairs;
    ubo_count = p_ubo_pair_count;
    feedbacks = p_feedback;
    feedback_count = p_feedback_count;

    //split vertex and shader code (thank you, shader compiler programmers from you know what company).
    {
        StringView globals_tag("\nVERTEX_SHADER_GLOBALS");
        StringView material_tag("\nMATERIAL_UNIFORMS");
        StringView code_tag("\nVERTEX_SHADER_CODE");
        StringView code(vertex_code);
        auto cpos = code.find(material_tag);
        if (cpos == String::npos) {
            vertex_code_before_mats = code;
        } else {
            vertex_code_before_mats = code.substr(0, cpos);
            code = code.substr(cpos + material_tag.length());

            cpos = code.find(globals_tag);

            if (cpos == String::npos) {
                vertex_code_before_globals = code;
            } else {

                vertex_code_before_globals = code.substr(0, cpos);
                StringView code2 = StringView(code).substr(cpos + globals_tag.length());

                cpos = code2.find(code_tag);
                if (cpos == code2.npos) {
                    vertex_code_before_custom = code2;
                } else {

                    vertex_code_before_custom = code2.substr(0, cpos);
                    vertex_code_after_custom = code2.substr(cpos + code_tag.length());
                }
            }
        }
    }
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_image_units);

    {
        StringView globals_tag("\nFRAGMENT_SHADER_GLOBALS");
        StringView material_tag("\nMATERIAL_UNIFORMS");
        StringView code_tag("\nFRAGMENT_SHADER_CODE");
        StringView light_code_tag("\nLIGHT_SHADER_CODE");
        StringView code(fragment_code);
        auto cpos = code.find(material_tag);
        if (cpos == code.npos) {
            fragment_code0 = code;
            return;
        }

        fragment_code0 = code.substr(0, cpos);
        //print_line("CODE0:\n"+String(fragment_code0.data()));
        code = code.substr(cpos + material_tag.length());
        cpos = code.find(globals_tag);

        if (cpos == code.npos) {
            fragment_code1 = code;
            return;
        }

        fragment_code1 = code.substr(0, cpos);
        //print_line("CODE1:\n"+String(fragment_code1.data()));

        StringView code2 = code.substr(cpos + globals_tag.length());
        cpos = code2.find(light_code_tag);

        if (cpos == code2.npos) {
            fragment_code2 = code2;
            return;
        }
        fragment_code2 = code2.substr(0, cpos);
        //print_line("CODE2:\n"+String(fragment_code2.data()));

        StringView code3 = code2.substr(cpos + light_code_tag.length());

        cpos = code3.find(code_tag);
        if (cpos == code3.npos) {
            fragment_code3 = code3;
            return;
        }
        fragment_code3 = code3.substr(0, cpos);
        //print_line("CODE3:\n"+String(fragment_code3.data()));
        fragment_code4 = code3.substr(cpos + code_tag.length());
        //print_line("CODE4:\n"+String(fragment_code4.data()));
    }


}

void ShaderGLES3::finish() {
    for(const auto &version : version_map) {
        const Version &v = version.second;
        glDeleteShader(v.vert_id);
        glDeleteShader(v.frag_id);
        glDeleteProgram(v.id);
        memdelete_arr(v.uniform_location);
    }
}

void ShaderGLES3::clear_caches() {

    for (const auto &version : version_map) {
        const Version &v = version.second;

        glDeleteShader(v.vert_id);
        glDeleteShader(v.frag_id);
        glDeleteProgram(v.id);
        memdelete_arr(v.uniform_location);
    }

    version_map.clear();

    custom_code_map.clear();
    version = nullptr;
    last_custom_code = 1;
    uniforms_dirty = true;
}

uint32_t ShaderGLES3::create_custom_shader() {

    custom_code_map[last_custom_code] = CustomCode();
    custom_code_map[last_custom_code].version = 1;
    return last_custom_code++;
}

void ShaderGLES3::set_custom_shader_code(uint32_t p_code_id, const String &p_vertex,
        const String &p_vertex_globals, const String &p_fragment, const String &p_light,
        const String &p_fragment_globals, const String &p_uniforms, const Vector<StringName> &p_texture_uniforms,
        const Vector<String> &p_custom_defines) {

    ERR_FAIL_COND(!custom_code_map.contains(p_code_id));
    CustomCode *cc = &custom_code_map[p_code_id];

    cc->vertex = p_vertex;
    cc->vertex_globals = p_vertex_globals;
    cc->fragment = p_fragment;
    cc->fragment_globals = p_fragment_globals;
    cc->light = p_light;
    cc->texture_uniforms = p_texture_uniforms;
    cc->uniforms = p_uniforms;
    cc->custom_defines = p_custom_defines;
    cc->version++;
}

void ShaderGLES3::set_custom_shader(uint32_t p_code_id) {

    new_conditional_version.code_version = p_code_id;
}

void ShaderGLES3::free_custom_shader(uint32_t p_code_id) {

    ERR_FAIL_COND(!custom_code_map.contains(p_code_id));
    if (conditional_version.code_version == p_code_id) {
        conditional_version.code_version = 0; //do not keep using a version that is going away
        unbind();
    }

    ShaderVersionKey key;
    key.code_version = p_code_id;
    for (uint32_t E : custom_code_map[p_code_id].versions) {
        key.version = E;
        ERR_CONTINUE(!version_map.contains(key));
        Version &v = version_map[key];

        glDeleteShader(v.vert_id);
        glDeleteShader(v.frag_id);
        glDeleteProgram(v.id);
        memdelete_arr(v.uniform_location);
        v.id = 0;

        version_map.erase(key);
    }

    custom_code_map.erase(p_code_id);
}

void ShaderGLES3::set_base_material_tex_index(int p_idx) {

    base_material_tex_index = p_idx;
}

ShaderGLES3::ShaderGLES3() {
    version = nullptr;
    last_custom_code = 1;
    uniforms_dirty = true;
    base_material_tex_index = 0;
}

ShaderGLES3::~ShaderGLES3() {

    finish();
}

/*************************************************************************/
/*  test_math.cpp                                                        */
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

#include "test_math.h"

#include "core/math/basis.h"
#include "core/math/camera_matrix.h"
#include "core/math/math_funcs.h"
#include "core/math/quat.h"
#include "core/math/transform.h"
#include "core/math/geometry.h"
#include "core/os/file_access.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/print_string.h"
#include "core/ustring.h"
#include "core/variant.h"
#include "scene/main/node.h"
#include "scene/resources/texture.h"
#include "servers/rendering/shader_language.h"
#include "core/class_db.h"

#include "core/method_ptrcall.h"

namespace TestMath {

class GetClassAndNamespace {

    String code;
    int idx;
    int line;
    String error_str;
    bool error;
    Variant value;

    String class_name;

    enum Token {
        TK_BRACKET_OPEN,
        TK_BRACKET_CLOSE,
        TK_CURLY_BRACKET_OPEN,
        TK_CURLY_BRACKET_CLOSE,
        TK_PERIOD,
        TK_COLON,
        TK_COMMA,
        TK_SYMBOL,
        TK_IDENTIFIER,
        TK_STRING,
        TK_NUMBER,
        TK_EOF,
        TK_ERROR
    };

    Token get_token() {

        while (true) {
            switch (code[idx]) {

                case '\n': {

                    line++;
                    idx++;
                    break;
                }
                case 0: {
                    return TK_EOF;

                }
                case '{': {

                    idx++;
                    return TK_CURLY_BRACKET_OPEN;
                }
                case '}': {

                    idx++;
                    return TK_CURLY_BRACKET_CLOSE;
                }
                case '[': {

                    idx++;
                    return TK_BRACKET_OPEN;
                }
                case ']': {

                    idx++;
                    return TK_BRACKET_CLOSE;
                }
                case ':': {

                    idx++;
                    return TK_COLON;
                }
                case ',': {

                    idx++;
                    return TK_COMMA;
                };
                case '.': {

                    idx++;
                    return TK_PERIOD;
                };
                case '#': {
                    //compiler directive
                    while (code[idx] != '\n' && code[idx] != 0) {
                        idx++;
                    }
                    continue;
                } break;
                case '/': {

                    switch (code[idx + 1]) {
                        case '*': { // block comment

                            idx += 2;
                            while (true) {
                                if (code[idx] == 0) {
                                    error_str = "Unterminated comment";
                                    error = true;
                                    return TK_ERROR;
                                } else if (code[idx] == '*' && code[idx + 1] == '/') {

                                    idx += 2;
                                    break;
                                } else if (code[idx] == '\n') {
                                    line++;
                                }

                                idx++;
                            }

                        } break;
                        case '/': { // line comment skip

                            while (code[idx] != '\n' && code[idx] != 0) {
                                idx++;
                            }

                        } break;
                        default: {
                            value = "/";
                            idx++;
                            return TK_SYMBOL;
                        }
                    }

                    continue; // a comment
                } break;
                case '\'':
                case '"': {

                    CharType begin_str = code[idx];
                    idx++;
                    String tk_string;
                    while (true) {
                        if (code[idx] == 0) {
                            error_str = "Unterminated String";
                            error = true;
                            return TK_ERROR;
                        } else if (code[idx] == begin_str) {
                            idx++;
                            break;
                        } else if (code[idx] == '\\') {
                            //escaped characters...
                            idx++;
                            char next = code[idx];
                            if (next == 0) {
                                error_str = "Unterminated String";
                                error = true;
                                return TK_ERROR;
                            }
                            char res = 0;

                            switch (next) {

                                case 'b': res = 8; break;
                                case 't': res = 9; break;
                                case 'n': res = 10; break;
                                case 'f': res = 12; break;
                                case 'r':
                                    res = 13;
                                    break;
                                case '\"': res = '\"'; break;
                                case '\\':
                                    res = '\\';
                                    break;
                                default: {
                                    res = next;
                                } break;
                            }

                            tk_string += res;

                        } else {
                            if (code[idx] == '\n')
                                line++;
                            tk_string += code[idx];
                        }
                        idx++;
                    }

                    value = tk_string;

                    return TK_STRING;

                } break;
                default: {

                    if (code[idx] <= 32) {
                        idx++;
                        break;
                    }
                    uint8_t c(code[idx]);
                    if ((c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 127)) {
                        value = {char(c)};
                        idx++;
                        return TK_SYMBOL;
                    }

                    if (code[idx] == '-' || (code[idx] >= '0' && code[idx] <= '9')) {
                        //a number
                        char *rptr;

                        double number = std::strtod(code.c_str()+idx,&rptr);
                        idx += (rptr - (code.c_str()+idx));
                        value = number;
                        return TK_NUMBER;

                    } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c > 127) {

                        String id;

                        while ((code[idx] >= 'A' && code[idx] <= 'Z') || (code[idx] >= 'a' && code[idx] <= 'z') || uint8_t(code[idx]) > 127) {

                            id += code[idx];
                            idx++;
                        }

                        value = id;
                        return TK_IDENTIFIER;
                    } else {
                        error_str = "Unexpected character.";
                        error = true;
                        return TK_ERROR;
                    }
                }
            }
        }
    }

public:
    Error parse(const String &p_code, const String &p_known_class_name = {}) {

        code = p_code;
        idx = 0;
        line = 0;
        error_str = String();
        error = false;
        value = Variant();
        class_name = String();

        bool use_next_class = false;
        Token tk = get_token();

        Map<int, String> namespace_stack;
        int curly_stack = 0;

        while (!error || tk != TK_EOF) {

            if (tk == TK_BRACKET_OPEN) {
                tk = get_token();
                if (tk == TK_IDENTIFIER && value.as<String>() == "ScriptClass") {
                    if (get_token() == TK_BRACKET_CLOSE) {
                        use_next_class = true;
                    }
                }
            } else if (tk == TK_IDENTIFIER && value.as<String>() == "class") {
                tk = get_token();
                if (tk == TK_IDENTIFIER) {
                    String name = value.as<String>();
                    if (use_next_class || p_known_class_name == name) {
                        for (eastl::pair<const int,String> &E : namespace_stack) {
                            class_name += E.second + ".";
                        }
                        class_name += value.as<String>();
                        break;
                    }
                }

            } else if (tk == TK_IDENTIFIER && value.as<String>() == "namespace") {
                String name;
                int at_level = curly_stack;
                while (true) {
                    tk = get_token();
                    if (tk == TK_IDENTIFIER) {
                        name += value.as<String>();
                    }

                    tk = get_token();
                    if (tk == TK_PERIOD) {
                        name += '.';
                    } else if (tk == TK_CURLY_BRACKET_OPEN) {
                        curly_stack++;
                        break;
                    } else {
                        break; //whathever else
                    }
                }

                if (!name.empty()) {
                    namespace_stack[at_level] = name;
                }

            } else if (tk == TK_CURLY_BRACKET_OPEN) {
                curly_stack++;
            } else if (tk == TK_CURLY_BRACKET_CLOSE) {
                curly_stack--;
                if (namespace_stack.contains(curly_stack)) {
                    namespace_stack.erase(curly_stack);
                }
            }

            tk = get_token();
        }

        if (error)
            return ERR_PARSE_ERROR;

        return OK;
    }

    String get_error() {
        return error_str;
    }

    String get_class() {
        return class_name;
    }
};

void test_vec(Plane p_vec) {

    CameraMatrix cm;
    cm.set_perspective(45, 1, 0, 100);
    Plane v0 = cm.xform4(p_vec);

    print_line("out: " + (String)v0);
    v0.normal.z = (v0.d / 100.0f * 2.0f - 1.0f) * v0.d;
    print_line("out_F: " + (String)v0);
}

uint32_t ihash(uint32_t a) {
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

uint32_t ihash2(uint32_t a) {
    a = (a ^ 61) ^ (a >> 16);
    a = a + (a << 3);
    a = a ^ (a >> 4);
    a = a * 0x27d4eb2d;
    a = a ^ (a >> 15);
    return a;
}

uint32_t ihash3(uint32_t a) {
    a = (a + 0x479ab41d) + (a << 8);
    a = (a ^ 0xe4aa10ce) ^ (a >> 5);
    a = (a + 0x9942f0a6) - (a << 14);
    a = (a ^ 0x5aedd67d) ^ (a >> 3);
    a = (a + 0x17bea992) + (a << 7);
    return a;
}

MainLoop *test() {

    {
        float r = 1;
        float g = 0.5f;
        float b = 0.1f;

        const float pow2to9 = 512.0f;
        const float B = 15.0f;
        const float N = 9.0f;

        float sharedexp = 65408.000f;

        float cRed = M_MAX(0.0f, MIN(sharedexp, r));
        float cGreen = M_MAX(0.0f, MIN(sharedexp, g));
        float cBlue = M_MAX(0.0f, MIN(sharedexp, b));

        float cMax = M_MAX(cRed, M_MAX(cGreen, cBlue));

        float expp = M_MAX(-B - 1.0f, floor(Math::log(cMax) / Math_LN2)) + 1.0f + B;

        float sMax = (float)floor((cMax / Math::pow(2.0f, expp - B - N)) + 0.5f);

        float exps = expp + 1.0f;

        if (0.0f <= sMax && sMax < pow2to9) {
            exps = expp;
        }

        float sRed = Math::floor((cRed / pow(2.0f, exps - B - N)) + 0.5f);
        float sGreen = Math::floor((cGreen / pow(2.0f, exps - B - N)) + 0.5f);
        float sBlue = Math::floor((cBlue / pow(2.0f, exps - B - N)) + 0.5f);

        print_line("R: " + rtos(sRed) + " G: " + rtos(sGreen) + " B: " + rtos(sBlue) + " EXP: " + rtos(exps));

        uint32_t rgbe = (Math::fast_ftoi(sRed) & 0x1FF) | ((Math::fast_ftoi(sGreen) & 0x1FF) << 9) | ((Math::fast_ftoi(sBlue) & 0x1FF) << 18) | ((Math::fast_ftoi(exps) & 0x1F) << 27);

        float rb = rgbe & 0x1ff;
        float gb = (rgbe >> 9) & 0x1ff;
        float bb = (rgbe >> 18) & 0x1ff;
        float eb = (rgbe >> 27);
        float mb = Math::pow(2, eb - 15.0f - 9.0f);
        float rd = rb * mb;
        float gd = gb * mb;
        float bd = bb * mb;

        print_line("RGBE: " + (String)Color(rd, gd, bd));
    }

    print_line("Dvectors: " + itos(MemoryPool::allocs_used));
    print_line("Mem used: " + itos(MemoryPool::total_memory));
    print_line("MAx mem used: " + itos(MemoryPool::max_memory));

    PoolVector<int> ints;
    ints.resize(20);

    {
        PoolVector<int>::Write w;
        w = ints.write();
        for (int i = 0; i < ints.size(); i++) {
            w[i] = i;
        }
    }

    PoolVector<int> posho = ints;

    {
        PoolVector<int>::Read r = posho.read();
        for (int i = 0; i < posho.size(); i++) {
            print_line(itos(i) + " : " + itos(r[i]));
        }
    }

    print_line("later Dvectors: " + itos(MemoryPool::allocs_used));
    print_line("later Mem used: " + itos(MemoryPool::total_memory));
    print_line("Mlater Ax mem used: " + itos(MemoryPool::max_memory));

    const Vector<String> &cmdlargs(OS::get_singleton()->get_cmdline_args());

    if (cmdlargs.empty()) {
        //try editor!
        return nullptr;
    }

    String test = cmdlargs.back();
    if (test == "math") {
        // Not a file name but the test name, abort.
        // FIXME: This test is ugly as heck, needs fixing :)
        return nullptr;
    }

    FileAccess *fa = FileAccess::open(test, FileAccess::READ);
    ERR_FAIL_COND_V_MSG(!fa, nullptr, "Could not open file: " + test);

    Vector<uint8_t> buf;
    int flen = fa->get_len();
    buf.resize(fa->get_len() + 1);
    fa->get_buffer(buf.data(), flen);
    buf[flen] = 0;

    String code((const char *)&buf[0]);

    GetClassAndNamespace getclass;
    if (getclass.parse(code)) {
        print_line("Parse error: " + getclass.get_error());
    } else {
        print_line("Found class: " + getclass.get_class());
    }

    {

        Vector<int> hashes;
        Vector<StringName> tl;
        ClassDB::get_class_list(&tl);

        for (size_t i=0,fin=tl.size(); i<fin; ++i) {

            auto m5b = StringUtils::md5_buffer(tl[i].asCString());
            hashes.push_back(hashes.size());
        }

        for (int i = nearest_shift(hashes.size()); i < 20; i++) {

            bool success = true;
            for (int s = 0; s < 10000; s++) {
                Set<uint32_t> existing;
                success = true;

                for (int j = 0; j < hashes.size(); j++) {

                    uint32_t eh = ihash2(ihash3(hashes[j] + ihash(s) + s)) & ((1 << i) - 1);
                    if (existing.contains(eh)) {
                        success = false;
                        break;
                    }
                    existing.insert(eh);
                }

                if (success) {
                    print_line("success at " + itos(i) + "/" + itos(nearest_shift(hashes.size())) + " shift " + itos(s));
                    break;
                }
            }
            if (success)
                break;
        }

        print_line("DONE");
    }

    {
        print_line("NUM: " + itos(-128));
    }

    {
        Vector3 v(1, 2, 3);
        v.normalize();
        float a = 0.3f;

        Basis m(v, a);

        Vector3 v2(7, 3, 1);
        v2.normalize();
        float a2 = 0.8f;

        Basis m2(v2, a2);

        Quat q = m;
        Quat q2 = m2;

        Basis m3 = m.inverse() * m2;
        Quat q3 = (q.inverse() * q2); //.normalized();

        print_line((String)Quat(m3));
        print_line((String)q3);

        print_line("before v: " + (String)v + " a: " + rtos(a));
        q.get_axis_angle(v, a);
        print_line("after v: " + (String)v + " a: " + rtos(a));
    }

    String ret;

    Vector<String> args {"-l"};
    Error err = OS::get_singleton()->execute("/bin/ls", args, true, nullptr, &ret);
    print_line("error: " + itos(err));
    print_line(ret);

    Basis m3;
    m3.rotate(Vector3(1, 0, 0), 0.2f);
    m3.rotate(Vector3(0, 1, 0), 1.77f);
    m3.rotate(Vector3(0, 0, 1), 212);
    Basis m32;
    m32.set_euler(m3.get_euler());
    print_line("ELEULEEEEEEEEEEEEEEEEEER: " + (String)m3.get_euler() + " vs " + (String)m32.get_euler());

    {
        Dictionary d;
        d["momo"] = 1;
        Dictionary b = d;
        b["44"] = 4;
    }

    print_line("inters: " + rtos(Geometry::segment_intersects_circle(Vector2(-5, 0), Vector2(-2, 0), Vector2(), 1.0)));

    print_line("cross: " + (String)Vector3(1, 2, 3).cross(Vector3(4, 5, 7)));
    print_line("dot: " + rtos(Vector3(1, 2, 3).dot(Vector3(4, 5, 7))));
    print_line("abs: " + (String)Vector3(-1, 2, -3).abs());
    print_line("distance_to: " + rtos(Vector3(1, 2, 3).distance_to(Vector3(4, 5, 7))));
    print_line("distance_squared_to: " + rtos(Vector3(1, 2, 3).distance_squared_to(Vector3(4, 5, 7))));
    print_line("plus: " + (String)(Vector3(1, 2, 3) + Vector3(Vector3(4, 5, 7))));
    print_line("minus: " + (String)(Vector3(1, 2, 3) - Vector3(Vector3(4, 5, 7))));
    print_line("mul: " + (String)(Vector3(1, 2, 3) * Vector3(Vector3(4, 5, 7))));
    print_line("div: " + (String)(Vector3(1, 2, 3) / Vector3(Vector3(4, 5, 7))));
    print_line("mul scalar: " + (String)(Vector3(1, 2, 3) * 2));
    print_line("premul scalar: " + (String)(2 * Vector3(1, 2, 3)));
    print_line("div scalar: " + (String)(Vector3(1, 2, 3) / 3.0));
    print_line("length: " + rtos(Vector3(1, 2, 3).length()));
    print_line("length squared: " + rtos(Vector3(1, 2, 3).length_squared()));
    print_line("normalized: " + (String)Vector3(1, 2, 3).normalized());
    print_line("inverse: " + (String)Vector3(1, 2, 3).inverse());

    {
        Vector3 v(4, 5, 7);
        v.normalize();
        print_line("normalize: " + (String)v);
    }

    {
        Vector3 v(4, 5, 7);
        v += Vector3(1, 2, 3);
        print_line("+=: " + (String)v);
    }

    {
        Vector3 v(4, 5, 7);
        v -= Vector3(1, 2, 3);
        print_line("-=: " + (String)v);
    }

    {
        Vector3 v(4, 5, 7);
        v *= Vector3(1, 2, 3);
        print_line("*=: " + (String)v);
    }

    {
        Vector3 v(4, 5, 7);
        v /= Vector3(1, 2, 3);
        print_line("/=: " + (String)v);
    }

    {
        Vector3 v(4, 5, 7);
        v *= 2.0;
        print_line("scalar *=: " + (String)v);
    }

    {
        Vector3 v(4, 5, 7);
        v /= 2.0;
        print_line("scalar /=: " + (String)v);
    }

    return nullptr;
}
} // namespace TestMath

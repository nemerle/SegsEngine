/*************************************************************************/
/*  audio_driver_dummy.h                                                 */
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

#pragma once

#include "servers/audio_server.h"

#include "core/os/thread.h"
#include "core/os/mutex.h"

class AudioDriverDummy : public AudioDriver {

    Thread thread;
    Mutex mutex;

    int32_t *samples_in;

    static void thread_func(void *p_udata);

    unsigned int buffer_frames;
    unsigned int mix_rate;
    SpeakerMode speaker_mode;

    int channels;

    bool active;
    bool thread_exited;
    mutable bool exit_thread;

public:
    const char *get_name() const override {
        return "Dummy";
    }

    Error init() override;
    void start() override;
    int get_mix_rate() const override;
    SpeakerMode get_speaker_mode() const override;
    void lock() override;
    void unlock() override;
    void finish() override;

    AudioDriverDummy();
    ~AudioDriverDummy() override;
};

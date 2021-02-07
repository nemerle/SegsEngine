/*************************************************************************/
/*  webrtc_data_channel.h                                                */
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

#include "core/io/packet_peer.h"
#include "core/method_enum_caster.h"

#define WRTC_IN_BUF "network/limits/webrtc/max_channel_in_buffer_kb"

class GODOT_EXPORT WebRTCDataChannel : public PacketPeer {
    GDCLASS(WebRTCDataChannel,PacketPeer)

public:
    enum WriteMode {
        WRITE_MODE_TEXT,
        WRITE_MODE_BINARY,
    };

    enum ChannelState {
        STATE_CONNECTING,
        STATE_OPEN,
        STATE_CLOSING,
        STATE_CLOSED
    };

protected:
    unsigned int _in_buffer_shift;
    static void _bind_methods();

public:
    virtual void set_write_mode(WriteMode mode) = 0;
    virtual WriteMode get_write_mode() const = 0;
    virtual bool was_string_packet() const = 0;

    virtual ChannelState get_ready_state() const = 0;
    virtual String get_label() const = 0;
    virtual bool is_ordered() const = 0;
    virtual int get_id() const = 0;
    virtual int get_max_packet_life_time() const = 0;
    virtual int get_max_retransmits() const = 0;
    virtual String get_protocol() const = 0;
    virtual bool is_negotiated() const = 0;

    virtual Error poll() = 0;
    virtual void close() = 0;

    /** Inherited from PacketPeer: **/
    int get_available_packet_count() const override = 0;
    Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override = 0; ///< buffer is GONE after next get_packet
    Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override = 0;

    int get_max_packet_size() const override = 0;

    WebRTCDataChannel();
    ~WebRTCDataChannel() override;
};


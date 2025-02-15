/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <ImageDecoder/ConnectionFromClient.h>
#include <ImageDecoder/ImageDecoderClientEndpoint.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImageFormats/ImageDecoder.h>

namespace ImageDecoder {

ConnectionFromClient::ConnectionFromClient(NonnullOwnPtr<Core::LocalSocket> socket)
    : IPC::ConnectionFromClient<ImageDecoderClientEndpoint, ImageDecoderServerEndpoint>(*this, move(socket), 1)
{
}

void ConnectionFromClient::die()
{
    Core::EventLoop::current().quit(0);
}

static void decode_image_to_bitmaps_and_durations_with_decoder(Gfx::ImageDecoder const& decoder, Vector<Gfx::ShareableBitmap>& bitmaps, Vector<u32>& durations)
{
    for (size_t i = 0; i < decoder.frame_count(); ++i) {
        auto frame_or_error = decoder.frame(i);
        if (frame_or_error.is_error()) {
            bitmaps.append(Gfx::ShareableBitmap {});
            durations.append(0);
        } else {
            auto frame = frame_or_error.release_value();
            bitmaps.append(frame.image->to_shareable_bitmap());
            durations.append(frame.duration);
        }
    }
}

static void decode_image_to_details(Core::AnonymousBuffer const& encoded_buffer, Optional<ByteString> const& known_mime_type, bool& is_animated, u32& loop_count, Vector<Gfx::ShareableBitmap>& bitmaps, Vector<u32>& durations)
{
    VERIFY(bitmaps.size() == 0);
    VERIFY(durations.size() == 0);

    auto decoder = Gfx::ImageDecoder::try_create_for_raw_bytes(ReadonlyBytes { encoded_buffer.data<u8>(), encoded_buffer.size() }, known_mime_type);
    if (!decoder) {
        dbgln_if(IMAGE_DECODER_DEBUG, "Could not find suitable image decoder plugin for data");
        return;
    }

    if (!decoder->frame_count()) {
        dbgln_if(IMAGE_DECODER_DEBUG, "Could not decode image from encoded data");
        return;
    }
    is_animated = decoder->is_animated();
    loop_count = decoder->loop_count();
    decode_image_to_bitmaps_and_durations_with_decoder(*decoder, bitmaps, durations);
}

Messages::ImageDecoderServer::DecodeImageResponse ConnectionFromClient::decode_image(Core::AnonymousBuffer const& encoded_buffer, Optional<ByteString> const& mime_type)
{
    if (!encoded_buffer.is_valid()) {
        dbgln_if(IMAGE_DECODER_DEBUG, "Encoded data is invalid");
        return nullptr;
    }

    bool is_animated = false;
    u32 loop_count = 0;
    Vector<Gfx::ShareableBitmap> bitmaps;
    Vector<u32> durations;
    decode_image_to_details(encoded_buffer, mime_type, is_animated, loop_count, bitmaps, durations);
    return { is_animated, loop_count, bitmaps, durations };
}

}

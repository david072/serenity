/*
 * Copyright (c) 2023, Nico Weber <thakis@chromium.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <AK/Endian.h>
#include <AK/Format.h>
#include <AK/MemoryStream.h>
#include <AK/Vector.h>
#include <LibGfx/ImageFormats/WebPLoader.h>
#include <LibGfx/ImageFormats/WebPLoaderLossless.h>
#include <LibGfx/Painter.h>

// Overview: https://developers.google.com/speed/webp/docs/compression
// Container: https://developers.google.com/speed/webp/docs/riff_container
// Lossy format: https://datatracker.ietf.org/doc/html/rfc6386

namespace Gfx {

namespace {

struct FourCC {
    constexpr FourCC(char const* name)
    {
        cc[0] = name[0];
        cc[1] = name[1];
        cc[2] = name[2];
        cc[3] = name[3];
    }

    bool operator==(FourCC const&) const = default;
    bool operator!=(FourCC const&) const = default;

    char cc[4];
};

// https://developers.google.com/speed/webp/docs/riff_container#webp_file_header
struct WebPFileHeader {
    FourCC riff;
    LittleEndian<u32> file_size;
    FourCC webp;
};
static_assert(AssertSize<WebPFileHeader, 12>());

struct ChunkHeader {
    FourCC chunk_type;
    LittleEndian<u32> chunk_size;
};
static_assert(AssertSize<ChunkHeader, 8>());

struct Chunk {
    FourCC type;
    ReadonlyBytes data;
};

struct VP8Header {
    u8 version;
    bool show_frame;
    u32 size_of_first_partition;
    u32 width;
    u8 horizontal_scale;
    u32 height;
    u8 vertical_scale;
};

struct VP8XHeader {
    bool has_icc;
    bool has_alpha;
    bool has_exif;
    bool has_xmp;
    bool has_animation;
    u32 width;
    u32 height;
};

struct ANIMChunk {
    u32 background_color;
    u16 loop_count;
};

struct ANMFChunk {
    u32 frame_x;
    u32 frame_y;
    u32 frame_width;
    u32 frame_height;
    u32 frame_duration_in_milliseconds;

    enum class BlendingMethod {
        UseAlphaBlending = 0,
        DoNotBlend = 1,
    };
    BlendingMethod blending_method;

    enum class DisposalMethod {
        DoNotDispose = 0,
        DisposeToBackgroundColor = 1,
    };
    DisposalMethod disposal_method;

    ReadonlyBytes frame_data;
};

// "For a still image, the image data consists of a single frame, which is made up of:
//     An optional alpha subchunk.
//     A bitstream subchunk."
struct ImageData {
    // "This optional chunk contains encoded alpha data for this frame. A frame containing a 'VP8L' chunk SHOULD NOT contain this chunk."
    Optional<Chunk> alpha_chunk; // 'ALPH'
    Chunk image_data_chunk;      // Either 'VP8 ' or 'VP8L'. For 'VP8L', alpha_chunk will not have a value.
};

}

struct WebPLoadingContext {
    enum State {
        NotDecoded = 0,
        Error,
        HeaderDecoded,
        FirstChunkRead,
        FirstChunkDecoded,
        ChunksDecoded,
        AnimationFrameChunksDecoded,
        BitmapDecoded,
    };
    State state { State::NotDecoded };
    ReadonlyBytes data;

    ReadonlyBytes chunks_cursor;

    Optional<IntSize> size;

    RefPtr<Gfx::Bitmap> bitmap;

    // Either 'VP8 ' (simple lossy file), 'VP8L' (simple lossless file), or 'VP8X' (extended file).
    Optional<Chunk> first_chunk;

    // Only valid if first_chunk->type == 'VP8X'.
    VP8XHeader vp8x_header;

    // If first_chunk is not a VP8X chunk, then only image_data.image_data_chunk is set and all the other Chunks are not set.
    // Once state is >= ChunksDecoded, for non-animated images, this will have a value, or decoding will have failed.
    Optional<ImageData> image_data;

    Optional<Chunk> animation_header_chunk; // 'ANIM'
    Vector<Chunk> animation_frame_chunks;   // 'ANMF'

    // These are set in state >= AnimationFrameChunksDecoded, if first_chunk.type == 'VP8X' && vp8x_header.has_animation.
    Optional<ANIMChunk> animation_header_chunk_data;
    Optional<Vector<ANMFChunk>> animation_frame_chunks_data;
    size_t current_frame { 0 };

    Optional<Chunk> iccp_chunk; // 'ICCP'
    Optional<Chunk> exif_chunk; // 'EXIF'
    Optional<Chunk> xmp_chunk;  // 'XMP '

    template<size_t N>
    [[nodiscard]] class Error error(char const (&string_literal)[N])
    {
        // FIXME: Remove this function; make all callers call Error::from_string_literal() directly.
        return Error::from_string_literal(string_literal);
    }
};

// https://developers.google.com/speed/webp/docs/riff_container#webp_file_header
static ErrorOr<void> decode_webp_header(WebPLoadingContext& context)
{
    if (context.state >= WebPLoadingContext::HeaderDecoded)
        return {};

    if (context.data.size() < sizeof(WebPFileHeader))
        return context.error("Missing WebP header");

    auto& header = *bit_cast<WebPFileHeader const*>(context.data.data());
    if (header.riff != FourCC("RIFF") || header.webp != FourCC("WEBP"))
        return context.error("Invalid WebP header");

    // "File Size: [...] The size of the file in bytes starting at offset 8. The maximum value of this field is 2^32 minus 10 bytes."
    u32 const maximum_webp_file_size = 0xffff'ffff - 9;
    if (header.file_size > maximum_webp_file_size)
        return context.error("WebP header file size over maximum");

    // "The file size in the header is the total size of the chunks that follow plus 4 bytes for the 'WEBP' FourCC.
    //  The file SHOULD NOT contain any data after the data specified by File Size.
    //  Readers MAY parse such files, ignoring the trailing data."
    if (context.data.size() - 8 < header.file_size)
        return context.error("WebP data too small for size in header");
    if (header.file_size < 4) // Need at least 4 bytes for 'WEBP', else we'll trim to less than the header size below.
        return context.error("WebP stored file size too small for header it's stored in");
    if (context.data.size() - 8 > header.file_size) {
        dbgln_if(WEBP_DEBUG, "WebP has {} bytes of data, but header needs only {}. Trimming.", context.data.size(), header.file_size + 8);
        context.data = context.data.trim(header.file_size + 8);
    }

    context.state = WebPLoadingContext::HeaderDecoded;
    return {};
}

// https://developers.google.com/speed/webp/docs/riff_container#riff_file_format
static ErrorOr<Chunk> decode_webp_chunk_header(WebPLoadingContext& context, ReadonlyBytes chunks)
{
    if (chunks.size() < sizeof(ChunkHeader))
        return context.error("Not enough data for WebP chunk header");

    auto const& header = *bit_cast<ChunkHeader const*>(chunks.data());
    dbgln_if(WEBP_DEBUG, "chunk {} size {}", header.chunk_type, header.chunk_size);

    if (chunks.size() < sizeof(ChunkHeader) + header.chunk_size)
        return context.error("Not enough data for WebP chunk");

    return Chunk { header.chunk_type, { chunks.data() + sizeof(ChunkHeader), header.chunk_size } };
}

// https://developers.google.com/speed/webp/docs/riff_container#riff_file_format
static ErrorOr<Chunk> decode_webp_advance_chunk(WebPLoadingContext& context, ReadonlyBytes& chunks)
{
    auto chunk = TRY(decode_webp_chunk_header(context, chunks));

    // "Chunk Size: 32 bits (uint32)
    //      The size of the chunk in bytes, not including this field, the chunk identifier or padding.
    //  Chunk Payload: Chunk Size bytes
    //      The data payload. If Chunk Size is odd, a single padding byte -- that MUST be 0 to conform with RIFF -- is added."
    chunks = chunks.slice(sizeof(ChunkHeader) + chunk.data.size());

    if (chunk.data.size() % 2 != 0) {
        if (chunks.is_empty())
            return context.error("Missing data for padding byte");
        if (*chunks.data() != 0)
            return context.error("Padding byte is not 0");
        chunks = chunks.slice(1);
    }

    return chunk;
}

// https://developers.google.com/speed/webp/docs/riff_container#simple_file_format_lossy
// https://datatracker.ietf.org/doc/html/rfc6386#section-19 "Annex A: Bitstream Syntax"
static ErrorOr<VP8Header> decode_webp_chunk_VP8_header(WebPLoadingContext& context, Chunk const& vp8_chunk)
{
    VERIFY(vp8_chunk.type == FourCC("VP8 "));

    if (vp8_chunk.data.size() < 10)
        return context.error("WebPImageDecoderPlugin: 'VP8 ' chunk too small");

    // FIXME: Eventually, this should probably call into LibVideo/VP8,
    // and image decoders should move into LibImageDecoders which depends on both LibGfx and LibVideo.
    // (LibVideo depends on LibGfx, so LibGfx can't depend on LibVideo itself.)

    // https://datatracker.ietf.org/doc/html/rfc6386#section-4 "Overview of Compressed Data Format"
    // "The decoder is simply presented with a sequence of compressed frames [...]
    //  The first frame presented to the decompressor is [...] a key frame.  [...]
    //  [E]very compressed frame has three or more pieces. It begins with an uncompressed data chunk comprising 10 bytes in the case of key frames

    u8 const* data = vp8_chunk.data.data();

    // https://datatracker.ietf.org/doc/html/rfc6386#section-9.1 "Uncompressed Data Chunk"
    u32 frame_tag = data[0] | (data[1] << 8) | (data[2] << 16);
    bool is_key_frame = (frame_tag & 1) == 0; // https://www.rfc-editor.org/errata/eid5534
    u8 version = (frame_tag & 0xe) >> 1;
    bool show_frame = (frame_tag & 0x10) != 0;
    u32 size_of_first_partition = frame_tag >> 5;

    if (!is_key_frame)
        return context.error("WebPImageDecoderPlugin: 'VP8 ' chunk not a key frame");

    // FIXME: !show_frame does not make sense in a webp file either, probably?

    u32 start_code = data[3] | (data[4] << 8) | (data[5] << 16);
    if (start_code != 0x2a019d) // https://www.rfc-editor.org/errata/eid7370
        return context.error("WebPImageDecoderPlugin: 'VP8 ' chunk invalid start_code");

    // "The scaling specifications for each dimension are encoded as follows.
    //   0     | No upscaling (the most common case).
    //   1     | Upscale by 5/4.
    //   2     | Upscale by 5/3.
    //   3     | Upscale by 2."
    // This is a display-time operation and doesn't affect decoding.
    u16 width_and_horizontal_scale = data[6] | (data[7] << 8);
    u16 width = width_and_horizontal_scale & 0x3fff;
    u8 horizontal_scale = width_and_horizontal_scale >> 14;

    u16 heigth_and_vertical_scale = data[8] | (data[9] << 8);
    u16 height = heigth_and_vertical_scale & 0x3fff;
    u8 vertical_scale = heigth_and_vertical_scale >> 14;

    dbgln_if(WEBP_DEBUG, "version {}, show_frame {}, size_of_first_partition {}, width {}, horizontal_scale {}, height {}, vertical_scale {}",
        version, show_frame, size_of_first_partition, width, horizontal_scale, height, vertical_scale);

    return VP8Header { version, show_frame, size_of_first_partition, width, horizontal_scale, height, vertical_scale };
}

static ErrorOr<NonnullRefPtr<Bitmap>> decode_webp_chunk_VP8(WebPLoadingContext& context, Chunk const& vp8_chunk)
{
    VERIFY(vp8_chunk.type == FourCC("VP8 "));

    // Uncomment this to test ALPH decoding for WebP-lossy-with-alpha images while lossy decoding isn't implemented yet.
#if 0
    auto vp8_header = TRY(decode_webp_chunk_VP8_header(context, vp8_chunk));

    // FIXME: probably want to pass in the bitmap format based on if there's an ALPH chunk.
    return Bitmap::create(BitmapFormat::BGRA8888, { vp8_header.width, vp8_header.height });
#else
    // FIXME: Implement webp lossy decoding.
    return context.error("WebPImageDecoderPlugin: decoding lossy webps not yet implemented");
#endif
}

static ErrorOr<NonnullRefPtr<Bitmap>> decode_webp_chunk_VP8L(WebPLoadingContext& context, Chunk const& vp8l_chunk)
{
    VERIFY(context.first_chunk->type == FourCC("VP8L") || context.first_chunk->type == FourCC("VP8X"));
    VERIFY(vp8l_chunk.type == FourCC("VP8L"));
    auto vp8l_header = TRY(decode_webp_chunk_VP8L_header(vp8l_chunk.data));
    return decode_webp_chunk_VP8L_contents(vp8l_header);
}

// https://developers.google.com/speed/webp/docs/riff_container#alpha
static ErrorOr<void> decode_webp_chunk_ALPH(WebPLoadingContext& context, Chunk const& alph_chunk, Bitmap& bitmap)
{
    VERIFY(alph_chunk.type == FourCC("ALPH"));

    if (alph_chunk.data.size() < 1)
        return context.error("WebPImageDecoderPlugin: ALPH chunk too small");

    u8 flags = alph_chunk.data.data()[0];
    u8 preprocessing = (flags >> 4) & 3;
    u8 filtering_method = (flags >> 2) & 3;
    u8 compression_method = flags & 3;

    dbgln_if(WEBP_DEBUG, "preprocessing {} filtering_method {} compression_method {}", preprocessing, filtering_method, compression_method);

    ReadonlyBytes alpha_data = alph_chunk.data.slice(1);

    size_t pixel_count = bitmap.width() * bitmap.height();

    if (compression_method == 0) {
        // "Raw data: consists of a byte sequence of length width * height, containing all the 8-bit transparency values in scan order."
        if (alpha_data.size() < pixel_count)
            return context.error("WebPImageDecoderPlugin: uncompressed ALPH data too small");
        for (size_t i = 0; i < pixel_count; ++i)
            bitmap.begin()[i] |= alpha_data[i] << 24;
        return {};
    }

    // "Lossless format compression: the byte sequence is a compressed image-stream (as described in the WebP Lossless Bitstream Format)
    //  of implicit dimension width x height. That is, this image-stream does NOT contain any headers describing the image dimension.
    //  Once the image-stream is decoded into ARGB color values, following the process described in the lossless format specification,
    //  the transparency information must be extracted from the green channel of the ARGB quadruplet."
    VP8LHeader vp8l_header { static_cast<u16>(bitmap.width()), static_cast<u16>(bitmap.height()), /*is_alpha_used=*/false, alpha_data };
    auto lossless_bitmap = TRY(decode_webp_chunk_VP8L_contents(vp8l_header));

    if (pixel_count != static_cast<size_t>(lossless_bitmap->width() * lossless_bitmap->height()))
        return context.error("WebPImageDecoderPlugin: decompressed ALPH dimensions don't match VP8 dimensions");

    for (size_t i = 0; i < pixel_count; ++i)
        bitmap.begin()[i] |= (lossless_bitmap->begin()[i] & 0xff00) << 16;

    return {};
}

static ErrorOr<VP8XHeader> decode_webp_chunk_VP8X(WebPLoadingContext& context, Chunk const& vp8x_chunk)
{
    VERIFY(vp8x_chunk.type == FourCC("VP8X"));

    // The VP8X chunk is documented at "Extended WebP file header:" at the end of
    // https://developers.google.com/speed/webp/docs/riff_container#extended_file_format
    if (vp8x_chunk.data.size() < 10)
        return context.error("WebPImageDecoderPlugin: VP8X chunk too small");

    u8 const* data = vp8x_chunk.data.data();

    // 1 byte flags
    // "Reserved (Rsv): 2 bits   MUST be 0. Readers MUST ignore this field.
    //  ICC profile (I): 1 bit   Set if the file contains an ICC profile.
    //  Alpha (L): 1 bit         Set if any of the frames of the image contain transparency information ("alpha").
    //  Exif metadata (E): 1 bit Set if the file contains Exif metadata.
    //  XMP metadata (X): 1 bit  Set if the file contains XMP metadata.
    //  Animation (A): 1 bit     Set if this is an animated image. Data in 'ANIM' and 'ANMF' chunks should be used to control the animation.
    //  Reserved (R): 1 bit      MUST be 0. Readers MUST ignore this field."
    u8 flags = data[0];
    bool has_icc = flags & 0x20;
    bool has_alpha = flags & 0x10;
    bool has_exif = flags & 0x8;
    bool has_xmp = flags & 0x4;
    bool has_animation = flags & 0x2;

    // 3 bytes reserved
    // 3 bytes width minus one
    u32 width = (data[4] | (data[5] << 8) | (data[6] << 16)) + 1;

    // 3 bytes height minus one
    u32 height = (data[7] | (data[8] << 8) | (data[9] << 16)) + 1;

    dbgln_if(WEBP_DEBUG, "flags 0x{:x} --{}{}{}{}{}{}, width {}, height {}",
        flags,
        has_icc ? " icc" : "",
        has_alpha ? " alpha" : "",
        has_exif ? " exif" : "",
        has_xmp ? " xmp" : "",
        has_animation ? " anim" : "",
        (flags & 0x3e) == 0 ? " none" : "",
        width, height);

    return VP8XHeader { has_icc, has_alpha, has_exif, has_xmp, has_animation, width, height };
}

// https://developers.google.com/speed/webp/docs/riff_container#animation
static ErrorOr<ANIMChunk> decode_webp_chunk_ANIM(WebPLoadingContext& context, Chunk const& anim_chunk)
{
    VERIFY(anim_chunk.type == FourCC("ANIM"));
    if (anim_chunk.data.size() < 6)
        return context.error("WebPImageDecoderPlugin: ANIM chunk too small");

    u8 const* data = anim_chunk.data.data();
    u32 background_color = (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) | ((u32)data[3] << 24);
    u16 loop_count = data[4] | (data[5] << 8);

    dbgln_if(WEBP_DEBUG, "background_color {:x} loop_count {}", background_color, loop_count);

    return ANIMChunk { background_color, loop_count };
}

// https://developers.google.com/speed/webp/docs/riff_container#animation
static ErrorOr<ANMFChunk> decode_webp_chunk_ANMF(WebPLoadingContext& context, Chunk const& anmf_chunk)
{
    VERIFY(anmf_chunk.type == FourCC("ANMF"));
    if (anmf_chunk.data.size() < 16)
        return context.error("WebPImageDecoderPlugin: ANMF chunk too small");

    u8 const* data = anmf_chunk.data.data();

    // "The X coordinate of the upper left corner of the frame is Frame X * 2."
    u32 frame_x = ((u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16)) * 2;

    // "The Y coordinate of the upper left corner of the frame is Frame Y * 2."
    u32 frame_y = ((u32)data[3] | ((u32)data[4] << 8) | ((u32)data[5] << 16)) * 2;

    // "The frame width is 1 + Frame Width Minus One."
    u32 frame_width = ((u32)data[6] | ((u32)data[7] << 8) | ((u32)data[8] << 16)) + 1;

    // "The frame height is 1 + Frame Height Minus One."
    u32 frame_height = ((u32)data[9] | ((u32)data[10] << 8) | ((u32)data[11] << 16)) + 1;

    // "The time to wait before displaying the next frame, in 1 millisecond units.
    //  Note the interpretation of frame duration of 0 (and often <= 10) is implementation defined.
    //  Many tools and browsers assign a minimum duration similar to GIF."
    u32 frame_duration = (u32)data[12] | ((u32)data[13] << 8) | ((u32)data[14] << 16);

    u8 flags = data[15];
    auto blending_method = static_cast<ANMFChunk::BlendingMethod>((flags >> 1) & 1);
    auto disposal_method = static_cast<ANMFChunk::DisposalMethod>(flags & 1);

    ReadonlyBytes frame_data = anmf_chunk.data.slice(16);

    dbgln_if(WEBP_DEBUG, "frame_x {} frame_y {} frame_width {} frame_height {} frame_duration {} blending_method {} disposal_method {}",
        frame_x, frame_y, frame_width, frame_height, frame_duration, (int)blending_method, (int)disposal_method);

    // https://developers.google.com/speed/webp/docs/riff_container#assembling_the_canvas_from_frames
    // "assert VP8X.canvasWidth >= frame_right
    //  assert VP8X.canvasHeight >= frame_bottom"
    VERIFY(context.first_chunk->type == FourCC("VP8X"));
    if (frame_x + frame_width > context.vp8x_header.width || frame_y + frame_height > context.vp8x_header.height)
        return context.error("WebPImageDecoderPlugin: ANMF dimensions out of bounds");

    return ANMFChunk { frame_x, frame_y, frame_width, frame_height, frame_duration, blending_method, disposal_method, frame_data };
}

static ErrorOr<ImageData> decode_webp_set_image_data(WebPLoadingContext& context, Optional<Chunk> alpha, Optional<Chunk> image_data)
{
    if (!image_data.has_value())
        return context.error("WebPImageDecoderPlugin: missing image data");

    // https://developers.google.com/speed/webp/docs/riff_container#alpha
    // "A frame containing a 'VP8L' chunk SHOULD NOT contain this chunk."
    if (alpha.has_value() && image_data->type == FourCC("VP8L")) {
        dbgln_if(WEBP_DEBUG, "WebPImageDecoderPlugin: VP8L frames should not have ALPH chunks. Ignoring ALPH chunk.");
        alpha.clear();
    }

    return ImageData { move(alpha), image_data.value() };
}

// https://developers.google.com/speed/webp/docs/riff_container#extended_file_format
static ErrorOr<void> decode_webp_extended(WebPLoadingContext& context, ReadonlyBytes chunks)
{
    VERIFY(context.first_chunk->type == FourCC("VP8X"));

    Optional<Chunk> alpha, image_data;

    // FIXME: This isn't quite to spec, which says
    // "All chunks SHOULD be placed in the same order as listed above.
    //  If a chunk appears in the wrong place, the file is invalid, but readers MAY parse the file, ignoring the chunks that are out of order."
    auto store = [](auto& field, Chunk const& chunk) {
        if (!field.has_value())
            field = chunk;
    };
    while (!chunks.is_empty()) {
        auto chunk = TRY(decode_webp_advance_chunk(context, chunks));

        if (chunk.type == FourCC("ICCP"))
            store(context.iccp_chunk, chunk);
        else if (chunk.type == FourCC("ALPH"))
            store(alpha, chunk);
        else if (chunk.type == FourCC("ANIM"))
            store(context.animation_header_chunk, chunk);
        else if (chunk.type == FourCC("ANMF"))
            TRY(context.animation_frame_chunks.try_append(chunk));
        else if (chunk.type == FourCC("EXIF"))
            store(context.exif_chunk, chunk);
        else if (chunk.type == FourCC("XMP "))
            store(context.xmp_chunk, chunk);
        else if (chunk.type == FourCC("VP8 ") || chunk.type == FourCC("VP8L"))
            store(image_data, chunk);
    }

    // Validate chunks.

    // https://developers.google.com/speed/webp/docs/riff_container#animation
    // "ANIM Chunk: [...] This chunk MUST appear if the Animation flag in the VP8X chunk is set. If the Animation flag is not set and this chunk is present, it MUST be ignored."
    if (context.vp8x_header.has_animation && !context.animation_header_chunk.has_value())
        return context.error("WebPImageDecoderPlugin: Header claims animation, but no ANIM chunk");
    if (!context.vp8x_header.has_animation && context.animation_header_chunk.has_value()) {
        dbgln_if(WEBP_DEBUG, "WebPImageDecoderPlugin: Header claims no animation, but ANIM chunk present. Ignoring ANIM chunk.");
        context.animation_header_chunk.clear();
    }

    // "ANMF Chunk: [...] If the Animation flag is not set, then this chunk SHOULD NOT be present."
    if (!context.vp8x_header.has_animation && !context.animation_frame_chunks.is_empty()) {
        dbgln_if(WEBP_DEBUG, "WebPImageDecoderPlugin: Header claims no animation, but ANMF chunks present. Ignoring ANMF chunks.");
        context.animation_frame_chunks.clear();
    }

    // Image data is not optional -- but the spec doesn't explicitly say that an animated image must have more than 0 frames.
    // The spec also doesn't say that animated images must not contain a regular image data segment.
    if (!context.vp8x_header.has_animation || image_data.has_value())
        context.image_data = TRY(decode_webp_set_image_data(context, move(alpha), move(image_data)));

    // https://developers.google.com/speed/webp/docs/riff_container#color_profile
    // "This chunk MUST appear before the image data."
    if (context.iccp_chunk.has_value()
        && ((context.image_data.has_value()
                && (context.iccp_chunk->data.data() > context.image_data->image_data_chunk.data.data()
                    || (context.image_data->alpha_chunk.has_value() && context.iccp_chunk->data.data() > context.image_data->alpha_chunk->data.data())))
            || (!context.animation_frame_chunks.is_empty() && context.iccp_chunk->data.data() > context.animation_frame_chunks[0].data.data()))) {
        return context.error("WebPImageDecoderPlugin: ICCP chunk is after image data");
    }

    context.state = WebPLoadingContext::State::ChunksDecoded;
    return {};
}

static ErrorOr<void> read_webp_first_chunk(WebPLoadingContext& context)
{
    if (context.state >= WebPLoadingContext::State::FirstChunkRead)
        return {};

    if (context.state < WebPLoadingContext::HeaderDecoded)
        TRY(decode_webp_header(context));

    context.chunks_cursor = context.data.slice(sizeof(WebPFileHeader));
    auto first_chunk = TRY(decode_webp_advance_chunk(context, context.chunks_cursor));

    if (first_chunk.type != FourCC("VP8 ") && first_chunk.type != FourCC("VP8L") && first_chunk.type != FourCC("VP8X"))
        return context.error("WebPImageDecoderPlugin: Invalid first chunk type");

    context.first_chunk = first_chunk;
    context.state = WebPLoadingContext::State::FirstChunkRead;

    if (first_chunk.type == FourCC("VP8 ") || first_chunk.type == FourCC("VP8L"))
        context.image_data = TRY(decode_webp_set_image_data(context, OptionalNone {}, first_chunk));

    return {};
}

static ErrorOr<void> decode_webp_first_chunk(WebPLoadingContext& context)
{
    if (context.state >= WebPLoadingContext::State::FirstChunkDecoded)
        return {};

    if (context.state < WebPLoadingContext::FirstChunkRead)
        TRY(read_webp_first_chunk(context));

    if (context.first_chunk->type == FourCC("VP8 ")) {
        auto vp8_header = TRY(decode_webp_chunk_VP8_header(context, context.first_chunk.value()));
        context.size = IntSize { vp8_header.width, vp8_header.height };
        context.state = WebPLoadingContext::State::FirstChunkDecoded;
        return {};
    }
    if (context.first_chunk->type == FourCC("VP8L")) {
        auto vp8l_header = TRY(decode_webp_chunk_VP8L_header(context.first_chunk->data));
        context.size = IntSize { vp8l_header.width, vp8l_header.height };
        context.state = WebPLoadingContext::State::FirstChunkDecoded;
        return {};
    }
    VERIFY(context.first_chunk->type == FourCC("VP8X"));
    context.vp8x_header = TRY(decode_webp_chunk_VP8X(context, context.first_chunk.value()));
    context.size = IntSize { context.vp8x_header.width, context.vp8x_header.height };
    context.state = WebPLoadingContext::State::FirstChunkDecoded;
    return {};
}

static ErrorOr<void> decode_webp_chunks(WebPLoadingContext& context)
{
    if (context.state >= WebPLoadingContext::State::ChunksDecoded)
        return {};

    if (context.state < WebPLoadingContext::FirstChunkDecoded)
        TRY(decode_webp_first_chunk(context));

    if (context.first_chunk->type == FourCC("VP8X"))
        return decode_webp_extended(context, context.chunks_cursor);

    context.state = WebPLoadingContext::State::ChunksDecoded;
    return {};
}

static ErrorOr<void> decode_webp_animation_frame_chunks(WebPLoadingContext& context)
{
    if (context.state >= WebPLoadingContext::State::AnimationFrameChunksDecoded)
        return {};

    context.animation_header_chunk_data = TRY(decode_webp_chunk_ANIM(context, context.animation_header_chunk.value()));

    Vector<ANMFChunk> decoded_chunks;
    TRY(decoded_chunks.try_ensure_capacity(context.animation_frame_chunks.size()));
    for (auto const& chunk : context.animation_frame_chunks)
        TRY(decoded_chunks.try_append(TRY(decode_webp_chunk_ANMF(context, chunk))));
    context.animation_frame_chunks_data = move(decoded_chunks);

    context.state = WebPLoadingContext::State::AnimationFrameChunksDecoded;
    return {};
}

static ErrorOr<ImageData> decode_webp_animation_frame_image_data(WebPLoadingContext& context, ANMFChunk const& frame)
{
    ReadonlyBytes chunks = frame.frame_data;
    auto chunk = TRY(decode_webp_advance_chunk(context, chunks));

    Optional<Chunk> alpha, image_data;
    if (chunk.type == FourCC("ALPH")) {
        alpha = chunk;
        chunk = TRY(decode_webp_advance_chunk(context, chunks));
    }
    if (chunk.type == FourCC("VP8 ") || chunk.type == FourCC("VP8L"))
        image_data = chunk;

    return decode_webp_set_image_data(context, move(alpha), move(image_data));
}

static ErrorOr<NonnullRefPtr<Bitmap>> decode_webp_image_data(WebPLoadingContext& context, ImageData const& image_data)
{
    if (image_data.image_data_chunk.type == FourCC("VP8L")) {
        VERIFY(!image_data.alpha_chunk.has_value());
        return decode_webp_chunk_VP8L(context, image_data.image_data_chunk);
    }

    VERIFY(image_data.image_data_chunk.type == FourCC("VP8 "));
    auto bitmap = TRY(decode_webp_chunk_VP8(context, image_data.image_data_chunk));

    if (image_data.alpha_chunk.has_value())
        TRY(decode_webp_chunk_ALPH(context, image_data.alpha_chunk.value(), *bitmap));

    return bitmap;
}

// https://developers.google.com/speed/webp/docs/riff_container#assembling_the_canvas_from_frames
static ErrorOr<ImageFrameDescriptor> decode_webp_animation_frame(WebPLoadingContext& context, size_t frame_index)
{
    if (frame_index >= context.animation_frame_chunks_data->size())
        return context.error("frame_index size too high");

    VERIFY(context.first_chunk->type == FourCC("VP8X"));
    VERIFY(context.vp8x_header.has_animation);

    Color clear_color = Color::from_argb(context.animation_header_chunk_data->background_color);

    size_t start_frame = context.current_frame + 1;
    dbgln_if(WEBP_DEBUG, "start_frame {} context.current_frame {}", start_frame, context.current_frame);
    if (context.state < WebPLoadingContext::State::BitmapDecoded) {
        start_frame = 0;
        context.bitmap = TRY(Bitmap::create(BitmapFormat::BGRA8888, { context.vp8x_header.width, context.vp8x_header.height }));
        context.bitmap->fill(clear_color);
    } else if (frame_index < context.current_frame) {
        start_frame = 0;
    }

    Painter painter(*context.bitmap);

    // FIXME: Honor context.animation_header_chunk_data.loop_count.

    for (size_t i = start_frame; i <= frame_index; ++i) {
        dbgln_if(WEBP_DEBUG, "drawing frame {} to produce frame {}", i, frame_index);

        auto const& frame_description = context.animation_frame_chunks_data.value()[i];

        if (i > 0) {
            auto const& previous_frame = context.animation_frame_chunks_data.value()[i - 1];
            if (previous_frame.disposal_method == ANMFChunk::DisposalMethod::DisposeToBackgroundColor)
                painter.clear_rect({ previous_frame.frame_x, previous_frame.frame_y, previous_frame.frame_width, previous_frame.frame_height }, clear_color);
        }

        auto frame_image_data = TRY(decode_webp_animation_frame_image_data(context, frame_description));
        auto frame_bitmap = TRY(decode_webp_image_data(context, frame_image_data));
        if (static_cast<u32>(frame_bitmap->width()) != frame_description.frame_width || static_cast<u32>(frame_bitmap->height()) != frame_description.frame_height)
            return context.error("WebPImageDecoderPlugin: decoded frame bitmap size doesn't match frame description size");

        // FIXME: "Alpha-blending SHOULD be done in linear color space..."
        bool apply_alpha = frame_description.blending_method == ANMFChunk::BlendingMethod::UseAlphaBlending;
        painter.blit({ frame_description.frame_x, frame_description.frame_y }, *frame_bitmap, { {}, frame_bitmap->size() }, /*opacity=*/1.0, apply_alpha);

        context.current_frame = i;
        context.state = WebPLoadingContext::State::BitmapDecoded;
    }

    return ImageFrameDescriptor { context.bitmap, static_cast<int>(context.animation_frame_chunks_data.value()[frame_index].frame_duration_in_milliseconds) };
}

WebPImageDecoderPlugin::WebPImageDecoderPlugin(ReadonlyBytes data, OwnPtr<WebPLoadingContext> context)
    : m_context(move(context))
{
    m_context->data = data;
}

WebPImageDecoderPlugin::~WebPImageDecoderPlugin() = default;

bool WebPImageDecoderPlugin::set_error(ErrorOr<void>&& error_or)
{
    if (error_or.is_error()) {
        m_context->state = WebPLoadingContext::State::Error;
        return true;
    }
    return false;
}

IntSize WebPImageDecoderPlugin::size()
{
    if (m_context->state == WebPLoadingContext::State::Error)
        return {};

    if (m_context->state < WebPLoadingContext::State::FirstChunkDecoded) {
        if (set_error(decode_webp_first_chunk(*m_context)))
            return {};
    }

    return m_context->size.value();
}

void WebPImageDecoderPlugin::set_volatile()
{
    if (m_context->bitmap)
        m_context->bitmap->set_volatile();
}

bool WebPImageDecoderPlugin::set_nonvolatile(bool& was_purged)
{
    if (!m_context->bitmap)
        return false;
    return m_context->bitmap->set_nonvolatile(was_purged);
}

bool WebPImageDecoderPlugin::initialize()
{
    return !set_error(decode_webp_header(*m_context));
}

bool WebPImageDecoderPlugin::sniff(ReadonlyBytes data)
{
    WebPLoadingContext context;
    context.data = data;

    // Intentionally no set_error() call: We're just sniffing `data` passed to the function, not reading m_context->data.
    return !decode_webp_header(context).is_error();
}

ErrorOr<NonnullOwnPtr<ImageDecoderPlugin>> WebPImageDecoderPlugin::create(ReadonlyBytes data)
{
    auto context = TRY(try_make<WebPLoadingContext>());
    return adopt_nonnull_own_or_enomem(new (nothrow) WebPImageDecoderPlugin(data, move(context)));
}

bool WebPImageDecoderPlugin::is_animated()
{
    if (m_context->state == WebPLoadingContext::State::Error)
        return false;

    if (m_context->state < WebPLoadingContext::State::FirstChunkDecoded) {
        if (set_error(decode_webp_first_chunk(*m_context)))
            return false;
    }

    return m_context->first_chunk->type == FourCC("VP8X") && m_context->vp8x_header.has_animation;
}

size_t WebPImageDecoderPlugin::loop_count()
{
    if (!is_animated())
        return 0;

    if (m_context->state < WebPLoadingContext::State::AnimationFrameChunksDecoded) {
        if (set_error(decode_webp_animation_frame_chunks(*m_context)))
            return 0;
    }

    return m_context->animation_header_chunk_data->loop_count;
}

size_t WebPImageDecoderPlugin::frame_count()
{
    if (!is_animated())
        return 1;

    if (m_context->state < WebPLoadingContext::State::ChunksDecoded) {
        if (set_error(decode_webp_chunks(*m_context)))
            return 1;
    }

    return m_context->animation_frame_chunks.size();
}

size_t WebPImageDecoderPlugin::first_animated_frame_index()
{
    return 0;
}

ErrorOr<ImageFrameDescriptor> WebPImageDecoderPlugin::frame(size_t index)
{
    if (index >= frame_count())
        return Error::from_string_literal("WebPImageDecoderPlugin: Invalid frame index");

    if (m_context->state == WebPLoadingContext::State::Error)
        return Error::from_string_literal("WebPImageDecoderPlugin: Decoding failed");

    // In a a lambda so that only one check to set State::Error is needed, instead of one per TRY.
    auto decode_frame = [this](size_t index) -> ErrorOr<ImageFrameDescriptor> {
        if (m_context->state < WebPLoadingContext::State::ChunksDecoded)
            TRY(decode_webp_chunks(*m_context));

        if (is_animated()) {
            if (m_context->state < WebPLoadingContext::State::AnimationFrameChunksDecoded)
                TRY(decode_webp_animation_frame_chunks(*m_context));
            return decode_webp_animation_frame(*m_context, index);
        }

        if (m_context->state < WebPLoadingContext::State::BitmapDecoded) {
            auto bitmap = TRY(decode_webp_image_data(*m_context, m_context->image_data.value()));

            // Check that size in VP8X chunk matches dimensions in VP8 or VP8L chunk if both are present.
            if (m_context->first_chunk->type == FourCC("VP8X")) {
                if (static_cast<u32>(bitmap->width()) != m_context->vp8x_header.width || static_cast<u32>(bitmap->height()) != m_context->vp8x_header.height)
                    return m_context->error("WebPImageDecoderPlugin: VP8X and VP8/VP8L chunks store different dimensions");
            }

            m_context->bitmap = move(bitmap);
            m_context->state = WebPLoadingContext::State::BitmapDecoded;
        }

        VERIFY(m_context->bitmap);
        return ImageFrameDescriptor { m_context->bitmap, 0 };
    };

    auto result = decode_frame(index);
    if (result.is_error()) {
        m_context->state = WebPLoadingContext::State::Error;
        return result.release_error();
    }
    return result.release_value();
}

ErrorOr<Optional<ReadonlyBytes>> WebPImageDecoderPlugin::icc_data()
{
    if (auto result = decode_webp_chunks(*m_context); result.is_error()) {
        m_context->state = WebPLoadingContext::State::Error;
        return result.release_error();
    }

    // FIXME: "If this chunk is not present, sRGB SHOULD be assumed."

    return m_context->iccp_chunk.map([](auto iccp_chunk) { return iccp_chunk.data; });
}

}

template<>
struct AK::Formatter<Gfx::FourCC> : StandardFormatter {
    ErrorOr<void> format(FormatBuilder& builder, Gfx::FourCC const& four_cc)
    {
        TRY(builder.put_padding('\'', 1));
        TRY(builder.put_padding(four_cc.cc[0], 1));
        TRY(builder.put_padding(four_cc.cc[1], 1));
        TRY(builder.put_padding(four_cc.cc[2], 1));
        TRY(builder.put_padding(four_cc.cc[3], 1));
        TRY(builder.put_padding('\'', 1));
        return {};
    }
};

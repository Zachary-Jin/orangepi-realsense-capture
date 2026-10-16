#include <librealsense2/rs.h>
#include <librealsense2/h/rs_frame.h>
#include <librealsense2/h/rs_pipeline.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int report_rs_error(const char *operation, rs2_error **error)
{
    if (error == NULL || *error == NULL) {
        return 0;
    }

    fprintf(stderr,
            "%s failed: %s\n  function: %s\n  arguments: %s\n",
            operation,
            rs2_get_error_message(*error),
            rs2_get_failed_function(*error),
            rs2_get_failed_args(*error));
    rs2_free_error(*error);
    *error = NULL;
    return -1;
}

static int parse_positive_int(const char *text, int allow_zero, int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > 1000000L ||
        parsed < (allow_zero ? 0L : 1L)) {
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [-o FILE] [-n COUNT] [-r WIDTHxHEIGHT] [-f FPS] "
            "[-s SKIP]\n"
            "\n"
            "Capture RGB frames from an Intel RealSense camera as PPM images.\n"
            "\n"
            "  -o FILE          output file/prefix (default: capture.ppm)\n"
            "  -n COUNT         frames to save; 0 means until Ctrl+C (default: 1)\n"
            "  -r WIDTHxHEIGHT  color resolution (default: 640x480)\n"
            "  -f FPS           frame rate (default: 30)\n"
            "  -s SKIP          warm-up frames to discard (default: 30)\n"
            "  --help           show this help\n",
            program);
}

static int parse_resolution(const char *text, int *width, int *height)
{
    char *separator = strchr(text, 'x');
    char width_text[32];
    size_t width_length;

    if (separator == NULL || separator == text || separator[1] == '\0') {
        return -1;
    }

    width_length = (size_t)(separator - text);
    if (width_length >= sizeof(width_text)) {
        return -1;
    }

    memcpy(width_text, text, width_length);
    width_text[width_length] = '\0';
    if (parse_positive_int(width_text, 0, width) != 0 ||
        parse_positive_int(separator + 1, 0, height) != 0) {
        return -1;
    }

    return 0;
}

static int make_output_name(char *buffer,
                            size_t buffer_size,
                            const char *requested_name,
                            int requested_count,
                            unsigned long long sequence)
{
    const char *extension;
    int written;

    if (requested_count == 1) {
        written = snprintf(buffer, buffer_size, "%s", requested_name);
    } else {
        size_t length = strlen(requested_name);
        extension = length >= 4 && strcmp(requested_name + length - 4, ".ppm") == 0
                        ? requested_name + length - 4
                        : NULL;
        if (extension != NULL) {
            written = snprintf(buffer,
                               buffer_size,
                               "%.*s_%06llu.ppm",
                               (int)(extension - requested_name),
                               requested_name,
                               sequence);
        } else {
            written = snprintf(buffer,
                               buffer_size,
                               "%s_%06llu.ppm",
                               requested_name,
                               sequence);
        }
    }

    return written >= 0 && (size_t)written < buffer_size ? 0 : -1;
}

static int save_rgb_ppm(const char *path,
                        const uint8_t *pixels,
                        int width,
                        int height,
                        int stride)
{
    FILE *file = fopen(path, "wb");
    int row;

    if (file == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    if (fprintf(file, "P6\n%d %d\n255\n", width, height) < 0) {
        fprintf(stderr, "Cannot write the PPM header to %s\n", path);
        fclose(file);
        return -1;
    }

    for (row = 0; row < height; ++row) {
        const uint8_t *source = pixels + (size_t)row * (size_t)stride;
        if (fwrite(source, 1, (size_t)width * 3U, file) !=
            (size_t)width * 3U) {
            fprintf(stderr, "Cannot write pixel data to %s\n", path);
            fclose(file);
            return -1;
        }
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "Cannot finish writing %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *output_name = "capture.ppm";
    int capture_count = 1;
    int width = 640;
    int height = 480;
    int fps = 30;
    int warmup_frames = 30;
    int exit_code = EXIT_FAILURE;
    int pipeline_started = 0;
    int i;
    unsigned long long saved = 0;
    rs2_error *error = NULL;
    rs2_context *context = NULL;
    rs2_device_list *devices = NULL;
    rs2_pipeline *pipeline = NULL;
    rs2_config *config = NULL;
    rs2_pipeline_profile *profile = NULL;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "Missing value after %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (strcmp(argv[i], "-o") == 0) {
            output_name = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0) {
            if (parse_positive_int(argv[++i], 1, &capture_count) != 0) {
                fprintf(stderr, "Invalid frame count: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "-r") == 0) {
            if (parse_resolution(argv[++i], &width, &height) != 0) {
                fprintf(stderr, "Invalid resolution: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "-f") == 0) {
            if (parse_positive_int(argv[++i], 0, &fps) != 0) {
                fprintf(stderr, "Invalid FPS: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "-s") == 0) {
            if (parse_positive_int(argv[++i], 1, &warmup_frames) != 0) {
                fprintf(stderr, "Invalid warm-up frame count: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    context = rs2_create_context(RS2_API_VERSION, &error);
    if (report_rs_error("rs2_create_context", &error) != 0) {
        goto cleanup;
    }

    devices = rs2_query_devices(context, &error);
    if (report_rs_error("rs2_query_devices", &error) != 0) {
        goto cleanup;
    }
    i = rs2_get_device_count(devices, &error);
    if (report_rs_error("rs2_get_device_count", &error) != 0) {
        goto cleanup;
    }
    if (i == 0) {
        fprintf(stderr, "No Intel RealSense camera was found.\n");
        goto cleanup;
    }
    printf("Found %d RealSense device(s).\n", i);

    pipeline = rs2_create_pipeline(context, &error);
    if (report_rs_error("rs2_create_pipeline", &error) != 0) {
        goto cleanup;
    }
    config = rs2_create_config(&error);
    if (report_rs_error("rs2_create_config", &error) != 0) {
        goto cleanup;
    }

    rs2_config_enable_stream(config,
                             RS2_STREAM_COLOR,
                             0,
                             width,
                             height,
                             RS2_FORMAT_RGB8,
                             fps,
                             &error);
    if (report_rs_error("rs2_config_enable_stream", &error) != 0) {
        goto cleanup;
    }

    profile = rs2_pipeline_start_with_config(pipeline, config, &error);
    if (report_rs_error("rs2_pipeline_start_with_config", &error) != 0) {
        fprintf(stderr,
                "The camera may not support %dx%d RGB8 at %d FPS.\n",
                width,
                height,
                fps);
        goto cleanup;
    }
    pipeline_started = 1;
    printf("Streaming %dx%d RGB8 at %d FPS; warming up for %d frames...\n",
           width,
           height,
           fps,
           warmup_frames);

    for (i = 0; i < warmup_frames && !stop_requested; ++i) {
        rs2_frame *frames = rs2_pipeline_wait_for_frames(
            pipeline, RS2_DEFAULT_TIMEOUT, &error);
        if (report_rs_error("warm-up frame", &error) != 0) {
            goto cleanup;
        }
        rs2_release_frame(frames);
    }

    while (!stop_requested && (capture_count == 0 || saved < (unsigned)capture_count)) {
        char path[4096];
        rs2_frame *frames = NULL;
        rs2_frame *color_frame = NULL;
        int frame_count;
        int frame_width;
        int frame_height;
        int stride;
        const uint8_t *pixels;

        frames = rs2_pipeline_wait_for_frames(
            pipeline, RS2_DEFAULT_TIMEOUT, &error);
        if (report_rs_error("rs2_pipeline_wait_for_frames", &error) != 0) {
            goto cleanup;
        }

        frame_count = rs2_embedded_frames_count(frames, &error);
        if (report_rs_error("rs2_embedded_frames_count", &error) != 0) {
            rs2_release_frame(frames);
            goto cleanup;
        }
        if (frame_count < 1) {
            fprintf(stderr, "The camera returned an empty frame set.\n");
            rs2_release_frame(frames);
            goto cleanup;
        }

        color_frame = rs2_extract_frame(frames, 0, &error);
        if (report_rs_error("rs2_extract_frame", &error) != 0) {
            rs2_release_frame(frames);
            goto cleanup;
        }
        frame_width = rs2_get_frame_width(color_frame, &error);
        if (report_rs_error("rs2_get_frame_width", &error) != 0) {
            rs2_release_frame(color_frame);
            rs2_release_frame(frames);
            goto cleanup;
        }
        frame_height = rs2_get_frame_height(color_frame, &error);
        if (report_rs_error("rs2_get_frame_height", &error) != 0) {
            rs2_release_frame(color_frame);
            rs2_release_frame(frames);
            goto cleanup;
        }
        stride = rs2_get_frame_stride_in_bytes(color_frame, &error);
        if (report_rs_error("rs2_get_frame_stride_in_bytes", &error) != 0) {
            rs2_release_frame(color_frame);
            rs2_release_frame(frames);
            goto cleanup;
        }
        pixels = (const uint8_t *)rs2_get_frame_data(color_frame, &error);
        if (report_rs_error("rs2_get_frame_data", &error) != 0) {
            rs2_release_frame(color_frame);
            rs2_release_frame(frames);
            goto cleanup;
        }

        ++saved;
        if (make_output_name(path,
                             sizeof(path),
                             output_name,
                             capture_count,
                             saved) != 0) {
            fprintf(stderr, "Output path is too long.\n");
            rs2_release_frame(color_frame);
            rs2_release_frame(frames);
            goto cleanup;
        }
        if (save_rgb_ppm(path, pixels, frame_width, frame_height, stride) != 0) {
            rs2_release_frame(color_frame);
            rs2_release_frame(frames);
            goto cleanup;
        }

        printf("Saved %s (%dx%d)\n", path, frame_width, frame_height);
        fflush(stdout);
        rs2_release_frame(color_frame);
        rs2_release_frame(frames);
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    if (pipeline_started) {
        rs2_pipeline_stop(pipeline, &error);
        report_rs_error("rs2_pipeline_stop", &error);
    }
    if (profile != NULL) {
        rs2_delete_pipeline_profile(profile);
    }
    if (config != NULL) {
        rs2_delete_config(config);
    }
    if (pipeline != NULL) {
        rs2_delete_pipeline(pipeline);
    }
    if (devices != NULL) {
        rs2_delete_device_list(devices);
    }
    if (context != NULL) {
        rs2_delete_context(context);
    }

    return exit_code;
}

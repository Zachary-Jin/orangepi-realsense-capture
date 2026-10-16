#include <librealsense2/rs.h>
#include <librealsense2/h/rs_frame.h>
#include <librealsense2/h/rs_pipeline.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define FRAME_FPS 60
#define WEB_STREAM_FPS 30
#define FRAME_BYTES ((size_t)FRAME_WIDTH * FRAME_HEIGHT * 3U)
#define DEPTH_MAX_METERS 20.0f
#define DEFAULT_PORT 8080
#define JPEG_BUFFER_LIMIT (4U * 1024U * 1024U)

typedef enum {
    STREAM_COLOR = 0,
    STREAM_DEPTH = 1
} stream_kind;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t frame_ready;
    uint8_t *jpeg[2];
    size_t jpeg_size[2];
    unsigned long long sequence[2];
    unsigned long long captured_frames;
    double capture_fps;
    int stream_clients;
    int active_connections;
} app_state;

typedef struct {
    stream_kind kind;
    const char *name;
    int input_fd;
    int output_fd;
    pid_t pid;
    pthread_t writer_thread;
    pthread_t reader_thread;
    pthread_mutex_t input_mutex;
    pthread_cond_t input_ready;
    uint8_t *latest_rgb;
    unsigned long long input_sequence;
    unsigned long long written_sequence;
    int writer_started;
    int reader_started;
    app_state *state;
} encoder;

typedef struct {
    int socket_fd;
    app_state *state;
} client_context;

static volatile sig_atomic_t stop_requested = 0;
static volatile sig_atomic_t fatal_error = 0;
static int listen_fd = -1;

static const char index_html_head[] =
    "<!doctype html>\n"
    "<html lang=\"zh-CN\">\n"
    "<head>\n"
    "<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>RealSense Live</title>\n"
    "<style>\n"
    ":root{color-scheme:dark;--bg:#0b0d0f;--surface:#15191d;--line:#2a3036;"
    "--text:#f3f5f7;--muted:#9ba5ae;--ok:#37d67a;--warn:#ffb020}"
    "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);"
    "font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"
    "\"Segoe UI\",sans-serif;letter-spacing:0}"
    ".app{min-height:100vh;display:flex;flex-direction:column}"
    "header{min-height:64px;display:flex;align-items:center;justify-content:space-between;"
    "gap:20px;padding:12px 24px;border-bottom:1px solid var(--line);background:#101316}"
    ".brand{display:flex;align-items:center;gap:12px;min-width:0}"
    ".mark{width:30px;height:30px;border:2px solid #eef1f4;position:relative;flex:none}"
    ".mark:after{content:\"\";position:absolute;inset:7px;background:#ff5b35}"
    "h1{font-size:17px;line-height:1.2;margin:0;font-weight:650}"
    ".sub{font-size:12px;color:var(--muted);margin-top:3px}"
    ".status{display:flex;align-items:center;gap:9px;font-size:13px;color:var(--muted)}"
    ".dot{width:9px;height:9px;border-radius:50%;background:var(--warn);"
    "box-shadow:0 0 0 3px rgba(255,176,32,.12)}"
    ".dot.live{background:var(--ok);box-shadow:0 0 0 3px rgba(55,214,122,.12)}"
    "main{width:100%;max-width:1540px;margin:0 auto;padding:24px;flex:1}"
    ".metrics{display:flex;gap:24px;align-items:center;margin-bottom:18px;color:var(--muted);"
    "font-size:13px;min-height:20px}"
    ".metric strong{color:var(--text);font-weight:600;margin-left:6px}"
    ".views{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}"
    ".view{background:var(--surface);border:1px solid var(--line);border-radius:8px;overflow:hidden}"
    ".view-head{height:48px;padding:0 16px;display:flex;align-items:center;"
    "justify-content:space-between;border-bottom:1px solid var(--line)}"
    ".view-title{font-size:14px;font-weight:600}.view-meta{font-size:12px;color:var(--muted)}"
    ".viewport{aspect-ratio:4/3;background:#050607;display:grid;place-items:center;overflow:hidden}"
    ".viewport img{display:block;width:100%;height:100%;object-fit:contain}"
    ".legend{height:38px;display:flex;align-items:center;gap:10px;padding:0 16px;"
    "border-top:1px solid var(--line);font-size:11px;color:var(--muted)}"
    ".scale{height:7px;flex:1;background:linear-gradient(90deg,#ff2638,#ffdd31,#2bd87b,#28d9ef,#315bff)}"
    "footer{padding:14px 24px;border-top:1px solid var(--line);font-size:11px;color:#727d86}"
    "@media(max-width:900px){header{padding:11px 16px}main{padding:16px}.views{grid-template-columns:1fr}"
    ".metrics{gap:12px;flex-wrap:wrap}.viewport{max-height:calc(100vh - 190px)}}"
    "@media(max-width:480px){h1{font-size:15px}.sub{display:none}.status span:last-child{display:none}"
    "main{padding:10px}.views{gap:10px}.metrics{margin:4px 2px 12px;font-size:12px}.view-head{height:42px}}"
    "</style>\n"
    "</head>\n";

static const char index_html_body[] =
    "<body><div class=\"app\">\n"
    "<header><div class=\"brand\"><div class=\"mark\"></div><div><h1>RealSense Live</h1>"
    "<div class=\"sub\">Intel RealSense D435i</div></div></div>"
    "<div class=\"status\"><i id=\"dot\" class=\"dot\"></i><span id=\"state\">连接中</span>"
    "<span>LIVE</span></div></header>\n"
    "<main><div class=\"metrics\"><span class=\"metric\">分辨率<strong>640 x 480</strong></span>"
    "<span class=\"metric\">目标帧率<strong>60 Hz</strong></span>"
    "<span class=\"metric\">实际帧率<strong id=\"fps\">--</strong></span>"
    "<span class=\"metric\">在线画面<strong id=\"clients\">--</strong></span></div>"
    "<section class=\"views\">"
    "<article class=\"view\"><div class=\"view-head\"><span class=\"view-title\">彩色画面</span>"
    "<span class=\"view-meta\">RGB8</span></div><div class=\"viewport\">"
    "<img src=\"/stream/color.mjpg\" alt=\"RealSense 彩色实时画面\"></div></article>"
    "<article class=\"view\"><div class=\"view-head\"><span class=\"view-title\">深度画面</span>"
    "<span class=\"view-meta\">Z16 / 0-20 m</span></div><div class=\"viewport\">"
    "<img src=\"/stream/depth.mjpg\" alt=\"RealSense 深度实时画面\"></div>"
    "<div class=\"legend\"><span>近 0 m</span><div class=\"scale\"></div><span>远 20 m</span></div></article>"
    "</section></main><footer>640 x 480 / 60 Hz &middot; RGB + Depth &middot; MJPEG</footer>\n"
    "</div><script>"
    "const dot=document.getElementById('dot'),state=document.getElementById('state'),"
    "fps=document.getElementById('fps'),clients=document.getElementById('clients');"
    "async function update(){try{const r=await fetch('/api/status',{cache:'no-store'});"
    "if(!r.ok)throw Error();const s=await r.json();dot.classList.add('live');state.textContent='已连接';"
    "fps.textContent=s.capture_fps.toFixed(1)+' Hz';clients.textContent=s.stream_clients;"
    "}catch(e){dot.classList.remove('live');state.textContent='连接中断';fps.textContent='--';clients.textContent='--'}}"
    "update();setInterval(update,1000);</script></body></html>\n";

static void on_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
    if (listen_fd >= 0) {
        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
        listen_fd = -1;
    }
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

static int parse_port(const char *text, int *port)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1 || value > 65535) {
        return -1;
    }
    *port = (int)value;
    return 0;
}

static float get_depth_scale(rs2_device *device)
{
    rs2_error *error = NULL;
    rs2_sensor_list *sensors = rs2_query_sensors(device, &error);
    float scale = 0.001f;
    int count;
    int i;

    if (report_rs_error("rs2_query_sensors", &error) != 0) {
        return scale;
    }
    count = rs2_get_sensors_count(sensors, &error);
    if (report_rs_error("rs2_get_sensors_count", &error) != 0) {
        rs2_delete_sensor_list(sensors);
        return scale;
    }

    for (i = 0; i < count; ++i) {
        rs2_sensor *sensor = rs2_create_sensor(sensors, i, &error);
        int is_depth;
        if (report_rs_error("rs2_create_sensor", &error) != 0) {
            continue;
        }
        is_depth = rs2_is_sensor_extendable_to(
            sensor, RS2_EXTENSION_DEPTH_SENSOR, &error);
        if (report_rs_error("rs2_is_sensor_extendable_to", &error) == 0 &&
            is_depth &&
            rs2_supports_option((const rs2_options *)sensor,
                                RS2_OPTION_DEPTH_UNITS,
                                &error)) {
            if (report_rs_error("rs2_supports_option", &error) == 0) {
                scale = rs2_get_option((const rs2_options *)sensor,
                                       RS2_OPTION_DEPTH_UNITS,
                                       &error);
                report_rs_error("rs2_get_option", &error);
            }
            rs2_delete_sensor(sensor);
            break;
        }
        report_rs_error("rs2_supports_option", &error);
        rs2_delete_sensor(sensor);
    }
    rs2_delete_sensor_list(sensors);
    return scale;
}

static int send_all(int fd, const void *data, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)data;

    while (size > 0) {
        ssize_t sent = send(fd, cursor, size, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        cursor += (size_t)sent;
        size -= (size_t)sent;
    }
    return 0;
}

static int write_all(int fd, const void *data, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)data;

    while (size > 0) {
        ssize_t written = write(fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        cursor += (size_t)written;
        size -= (size_t)written;
    }
    return 0;
}

static int set_close_on_exec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0 ? 0 : -1;
}

static int create_pipe(int descriptors[2])
{
    if (pipe(descriptors) != 0) {
        return -1;
    }
    if (set_close_on_exec(descriptors[0]) != 0 ||
        set_close_on_exec(descriptors[1]) != 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        return -1;
    }
    return 0;
}

static int publish_jpeg(app_state *state,
                        stream_kind kind,
                        const uint8_t *data,
                        size_t size)
{
    uint8_t *resized;

    pthread_mutex_lock(&state->mutex);
    resized = realloc(state->jpeg[kind], size);
    if (resized == NULL) {
        pthread_mutex_unlock(&state->mutex);
        return -1;
    }
    state->jpeg[kind] = resized;
    memcpy(state->jpeg[kind], data, size);
    state->jpeg_size[kind] = size;
    ++state->sequence[kind];
    pthread_cond_broadcast(&state->frame_ready);
    pthread_mutex_unlock(&state->mutex);
    return 0;
}

static void *encoder_reader(void *argument)
{
    encoder *enc = (encoder *)argument;
    uint8_t input[16384];
    uint8_t *frame = malloc(JPEG_BUFFER_LIMIT);
    size_t frame_size = 0;
    int in_frame = 0;
    uint8_t previous = 0;

    if (frame == NULL) {
        fprintf(stderr, "Cannot allocate the %s JPEG buffer.\n", enc->name);
        fatal_error = 1;
        stop_requested = 1;
        return NULL;
    }

    while (1) {
        ssize_t count = read(enc->output_fd, input, sizeof(input));
        size_t i;
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (count == 0) {
            break;
        }

        for (i = 0; i < (size_t)count; ++i) {
            uint8_t byte = input[i];
            if (!in_frame) {
                if (previous == 0xff && byte == 0xd8) {
                    frame[0] = 0xff;
                    frame[1] = 0xd8;
                    frame_size = 2;
                    in_frame = 1;
                }
                previous = byte;
                continue;
            }

            if (frame_size >= JPEG_BUFFER_LIMIT) {
                fprintf(stderr, "%s JPEG frame exceeded the buffer limit.\n", enc->name);
                frame_size = 0;
                in_frame = 0;
                previous = byte;
                continue;
            }
            frame[frame_size++] = byte;
            if (previous == 0xff && byte == 0xd9) {
                if (publish_jpeg(enc->state, enc->kind, frame, frame_size) != 0) {
                    fatal_error = 1;
                    stop_requested = 1;
                    free(frame);
                    return NULL;
                }
                frame_size = 0;
                in_frame = 0;
            }
            previous = byte;
        }
    }

    free(frame);
    return NULL;
}

static void *encoder_writer(void *argument)
{
    encoder *enc = (encoder *)argument;
    uint8_t *frame = malloc(FRAME_BYTES);

    if (frame == NULL) {
        fprintf(stderr, "Cannot allocate the %s encoder input buffer.\n", enc->name);
        fatal_error = 1;
        stop_requested = 1;
        return NULL;
    }

    while (!stop_requested) {
        pthread_mutex_lock(&enc->input_mutex);
        while (!stop_requested && enc->input_sequence == enc->written_sequence) {
            pthread_cond_wait(&enc->input_ready, &enc->input_mutex);
        }
        if (stop_requested) {
            pthread_mutex_unlock(&enc->input_mutex);
            break;
        }
        memcpy(frame, enc->latest_rgb, FRAME_BYTES);
        enc->written_sequence = enc->input_sequence;
        pthread_mutex_unlock(&enc->input_mutex);

        if (write_all(enc->input_fd, frame, FRAME_BYTES) != 0) {
            fprintf(stderr, "%s FFmpeg encoder stopped unexpectedly.\n", enc->name);
            fatal_error = 1;
            stop_requested = 1;
            break;
        }
    }

    free(frame);
    return NULL;
}

static int queue_encoder_frame(encoder *enc, const uint8_t *rgb)
{
    pthread_mutex_lock(&enc->input_mutex);
    memcpy(enc->latest_rgb, rgb, FRAME_BYTES);
    ++enc->input_sequence;
    pthread_cond_signal(&enc->input_ready);
    pthread_mutex_unlock(&enc->input_mutex);
    return 0;
}

static int start_encoder(encoder *enc,
                         app_state *state,
                         stream_kind kind,
                         const char *name)
{
    int input_pipe[2];
    int output_pipe[2];

    memset(enc, 0, sizeof(*enc));
    enc->input_fd = -1;
    enc->output_fd = -1;
    enc->state = state;
    enc->kind = kind;
    enc->name = name;
    enc->latest_rgb = malloc(FRAME_BYTES);
    if (enc->latest_rgb == NULL) {
        fprintf(stderr, "Cannot allocate the %s latest-frame buffer.\n", name);
        return -1;
    }
    pthread_mutex_init(&enc->input_mutex, NULL);
    pthread_cond_init(&enc->input_ready, NULL);

    if (create_pipe(input_pipe) != 0) {
        perror("pipe");
        free(enc->latest_rgb);
        enc->latest_rgb = NULL;
        pthread_cond_destroy(&enc->input_ready);
        pthread_mutex_destroy(&enc->input_mutex);
        return -1;
    }
    if (create_pipe(output_pipe) != 0) {
        perror("pipe");
        close(input_pipe[0]);
        close(input_pipe[1]);
        free(enc->latest_rgb);
        enc->latest_rgb = NULL;
        pthread_cond_destroy(&enc->input_ready);
        pthread_mutex_destroy(&enc->input_mutex);
        return -1;
    }

    enc->pid = fork();
    if (enc->pid < 0) {
        perror("fork");
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        free(enc->latest_rgb);
        enc->latest_rgb = NULL;
        pthread_cond_destroy(&enc->input_ready);
        pthread_mutex_destroy(&enc->input_mutex);
        return -1;
    }

    if (enc->pid == 0) {
        char size_text[32];
        char fps_text[16];
        snprintf(size_text, sizeof(size_text), "%dx%d", FRAME_WIDTH, FRAME_HEIGHT);
        snprintf(fps_text, sizeof(fps_text), "%d", WEB_STREAM_FPS);
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execlp("ffmpeg",
               "ffmpeg",
               "-hide_banner",
               "-loglevel",
               "error",
               "-f",
               "rawvideo",
               "-pixel_format",
               "rgb24",
               "-video_size",
               size_text,
               "-framerate",
               fps_text,
               "-i",
               "pipe:0",
               "-an",
               "-c:v",
               "mjpeg",
               "-q:v",
               "5",
               "-f",
               "image2pipe",
               "pipe:1",
               (char *)NULL);
        perror("execlp ffmpeg");
        _exit(127);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);
    enc->input_fd = input_pipe[1];
    enc->output_fd = output_pipe[0];
    if (pthread_create(&enc->reader_thread, NULL, encoder_reader, enc) != 0) {
        perror("pthread_create encoder_reader");
        close(enc->input_fd);
        close(enc->output_fd);
        kill(enc->pid, SIGTERM);
        waitpid(enc->pid, NULL, 0);
        enc->input_fd = -1;
        enc->output_fd = -1;
        free(enc->latest_rgb);
        enc->latest_rgb = NULL;
        pthread_cond_destroy(&enc->input_ready);
        pthread_mutex_destroy(&enc->input_mutex);
        return -1;
    }
    enc->reader_started = 1;
    if (pthread_create(&enc->writer_thread, NULL, encoder_writer, enc) != 0) {
        perror("pthread_create encoder_writer");
        close(enc->input_fd);
        enc->input_fd = -1;
        kill(enc->pid, SIGTERM);
        waitpid(enc->pid, NULL, 0);
        pthread_join(enc->reader_thread, NULL);
        enc->reader_started = 0;
        close(enc->output_fd);
        enc->output_fd = -1;
        free(enc->latest_rgb);
        enc->latest_rgb = NULL;
        pthread_cond_destroy(&enc->input_ready);
        pthread_mutex_destroy(&enc->input_mutex);
        return -1;
    }
    enc->writer_started = 1;
    return 0;
}

static void stop_encoder(encoder *enc)
{
    pthread_mutex_lock(&enc->input_mutex);
    pthread_cond_broadcast(&enc->input_ready);
    pthread_mutex_unlock(&enc->input_mutex);
    if (enc->writer_started) {
        pthread_join(enc->writer_thread, NULL);
        enc->writer_started = 0;
    }
    if (enc->input_fd >= 0) {
        close(enc->input_fd);
        enc->input_fd = -1;
    }
    if (enc->pid > 0) {
        waitpid(enc->pid, NULL, 0);
    }
    if (enc->output_fd >= 0) {
        close(enc->output_fd);
        enc->output_fd = -1;
    }
    if (enc->reader_started) {
        pthread_join(enc->reader_thread, NULL);
        enc->reader_started = 0;
    }
    free(enc->latest_rgb);
    enc->latest_rgb = NULL;
    pthread_cond_destroy(&enc->input_ready);
    pthread_mutex_destroy(&enc->input_mutex);
}

static void depth_color(float value, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    float segment;
    int ramp;

    if (value < 0.0f) {
        value = 0.0f;
    } else if (value > 1.0f) {
        value = 1.0f;
    }
    segment = value * 4.0f;
    ramp = (int)((segment - (int)segment) * 255.0f);

    if (segment < 1.0f) {
        *red = 255;
        *green = (uint8_t)ramp;
        *blue = 40;
    } else if (segment < 2.0f) {
        *red = (uint8_t)(255 - ramp);
        *green = 220;
        *blue = 40;
    } else if (segment < 3.0f) {
        *red = 35;
        *green = (uint8_t)(220 - ramp / 3);
        *blue = (uint8_t)(60 + ramp * 3 / 4);
    } else {
        *red = 45;
        *green = (uint8_t)(135 - ramp / 3);
        *blue = 255;
    }
}

static void colorize_depth(const uint16_t *depth,
                           int depth_stride,
                           float depth_scale,
                           uint8_t *rgb)
{
    int y;
    for (y = 0; y < FRAME_HEIGHT; ++y) {
        const uint16_t *row = (const uint16_t *)((const uint8_t *)depth +
                                                (size_t)y * depth_stride);
        int x;
        for (x = 0; x < FRAME_WIDTH; ++x) {
            size_t output = ((size_t)y * FRAME_WIDTH + (size_t)x) * 3U;
            if (row[x] == 0) {
                rgb[output] = 0;
                rgb[output + 1] = 0;
                rgb[output + 2] = 0;
            } else {
                float meters = (float)row[x] * depth_scale;
                depth_color(meters / DEPTH_MAX_METERS,
                            &rgb[output],
                            &rgb[output + 1],
                            &rgb[output + 2]);
            }
        }
    }
}

static int copy_color_frame(const rs2_frame *frame,
                            const uint8_t *source,
                            uint8_t *destination,
                            rs2_error **error)
{
    int stride = rs2_get_frame_stride_in_bytes(frame, error);
    int y;
    if (*error != NULL) {
        return -1;
    }
    for (y = 0; y < FRAME_HEIGHT; ++y) {
        memcpy(destination + (size_t)y * FRAME_WIDTH * 3U,
               source + (size_t)y * (size_t)stride,
               (size_t)FRAME_WIDTH * 3U);
    }
    return 0;
}

static int send_simple_response(int fd,
                                const char *status,
                                const char *content_type,
                                const char *body,
                                size_t body_size)
{
    char header[512];
    int length = snprintf(header,
                          sizeof(header),
                          "HTTP/1.1 %s\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %zu\r\n"
                          "Cache-Control: no-store\r\n"
                          "Connection: close\r\n\r\n",
                          status,
                          content_type,
                          body_size);
    if (length < 0 || (size_t)length >= sizeof(header)) {
        return -1;
    }
    return send_all(fd, header, (size_t)length) == 0 &&
                   send_all(fd, body, body_size) == 0
               ? 0
               : -1;
}

static int send_html_response(int fd)
{
    char header[256];
    size_t body_size = sizeof(index_html_head) - 1 + sizeof(index_html_body) - 1;
    int length = snprintf(header,
                          sizeof(header),
                          "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html; charset=utf-8\r\n"
                          "Content-Length: %zu\r\n"
                          "Cache-Control: no-store\r\n"
                          "Connection: close\r\n\r\n",
                          body_size);
    if (length < 0 || (size_t)length >= sizeof(header)) {
        return -1;
    }
    return send_all(fd, header, (size_t)length) == 0 &&
                   send_all(fd, index_html_head, sizeof(index_html_head) - 1) == 0 &&
                   send_all(fd, index_html_body, sizeof(index_html_body) - 1) == 0
               ? 0
               : -1;
}

static void stream_mjpeg(int fd, app_state *state, stream_kind kind)
{
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n\r\n";
    uint8_t *local_frame = NULL;
    size_t local_capacity = 0;
    unsigned long long last_sequence = 0;

    if (send_all(fd, response, sizeof(response) - 1) != 0) {
        return;
    }

    pthread_mutex_lock(&state->mutex);
    ++state->stream_clients;
    pthread_mutex_unlock(&state->mutex);

    while (!stop_requested) {
        size_t frame_size;
        char part_header[160];
        int header_size;
        struct timespec timeout;

        pthread_mutex_lock(&state->mutex);
        clock_gettime(CLOCK_REALTIME, &timeout);
        ++timeout.tv_sec;
        while (!stop_requested && state->sequence[kind] == last_sequence) {
            if (pthread_cond_timedwait(&state->frame_ready,
                                       &state->mutex,
                                       &timeout) == ETIMEDOUT) {
                break;
            }
        }
        if (stop_requested || state->sequence[kind] == last_sequence) {
            pthread_mutex_unlock(&state->mutex);
            continue;
        }
        frame_size = state->jpeg_size[kind];
        if (frame_size > local_capacity) {
            uint8_t *resized = realloc(local_frame, frame_size);
            if (resized == NULL) {
                pthread_mutex_unlock(&state->mutex);
                break;
            }
            local_frame = resized;
            local_capacity = frame_size;
        }
        memcpy(local_frame, state->jpeg[kind], frame_size);
        last_sequence = state->sequence[kind];
        pthread_mutex_unlock(&state->mutex);

        header_size = snprintf(part_header,
                               sizeof(part_header),
                               "--frame\r\nContent-Type: image/jpeg\r\n"
                               "Content-Length: %zu\r\n\r\n",
                               frame_size);
        if (header_size < 0 || (size_t)header_size >= sizeof(part_header) ||
            send_all(fd, part_header, (size_t)header_size) != 0 ||
            send_all(fd, local_frame, frame_size) != 0 ||
            send_all(fd, "\r\n", 2) != 0) {
            break;
        }
    }

    pthread_mutex_lock(&state->mutex);
    --state->stream_clients;
    pthread_mutex_unlock(&state->mutex);
    free(local_frame);
}

static void *handle_client(void *argument)
{
    client_context *context = (client_context *)argument;
    int fd = context->socket_fd;
    app_state *state = context->state;
    char request[4096];
    size_t used = 0;
    struct timeval socket_timeout = {.tv_sec = 5, .tv_usec = 0};

    free(context);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout, sizeof(socket_timeout));

    while (used + 1 < sizeof(request)) {
        ssize_t count = recv(fd, request + used, sizeof(request) - used - 1, 0);
        if (count <= 0) {
            goto done;
        }
        used += (size_t)count;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL) {
            break;
        }
    }

    if (strncmp(request, "GET / ", 6) == 0 ||
        strncmp(request, "GET /index.html ", 16) == 0) {
        send_html_response(fd);
    } else if (strncmp(request, "GET /api/status", 15) == 0) {
        char json[256];
        double fps;
        unsigned long long frames;
        int clients;
        int size;
        pthread_mutex_lock(&state->mutex);
        fps = state->capture_fps;
        frames = state->captured_frames;
        clients = state->stream_clients;
        pthread_mutex_unlock(&state->mutex);
        size = snprintf(json,
                        sizeof(json),
                        "{\"capture_fps\":%.2f,\"captured_frames\":%llu,"
                        "\"stream_clients\":%d,\"width\":%d,\"height\":%d,"
                        "\"target_fps\":%d}\n",
                        fps,
                        frames,
                        clients,
                        FRAME_WIDTH,
                        FRAME_HEIGHT,
                        FRAME_FPS);
        send_simple_response(fd,
                             "200 OK",
                             "application/json; charset=utf-8",
                             json,
                             (size_t)size);
    } else if (strncmp(request, "GET /stream/color.mjpg", 22) == 0) {
        stream_mjpeg(fd, state, STREAM_COLOR);
    } else if (strncmp(request, "GET /stream/depth.mjpg", 22) == 0) {
        stream_mjpeg(fd, state, STREAM_DEPTH);
    } else {
        static const char body[] = "Not found\n";
        send_simple_response(fd,
                             "404 Not Found",
                             "text/plain; charset=utf-8",
                             body,
                             sizeof(body) - 1);
    }

done:
    close(fd);
    pthread_mutex_lock(&state->mutex);
    --state->active_connections;
    pthread_cond_broadcast(&state->frame_ready);
    pthread_mutex_unlock(&state->mutex);
    return NULL;
}

static void *http_server(void *argument)
{
    app_state *state = (app_state *)argument;

    while (!stop_requested) {
        int client_fd = accept(listen_fd, NULL, NULL);
        client_context *context;
        pthread_t thread;
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!stop_requested) {
                perror("accept");
                fatal_error = 1;
                stop_requested = 1;
            }
            break;
        }
        context = malloc(sizeof(*context));
        if (context == NULL) {
            close(client_fd);
            continue;
        }
        context->socket_fd = client_fd;
        context->state = state;
        pthread_mutex_lock(&state->mutex);
        ++state->active_connections;
        pthread_mutex_unlock(&state->mutex);
        if (pthread_create(&thread, NULL, handle_client, context) != 0) {
            close(client_fd);
            free(context);
            pthread_mutex_lock(&state->mutex);
            --state->active_connections;
            pthread_cond_broadcast(&state->frame_ready);
            pthread_mutex_unlock(&state->mutex);
            continue;
        }
        pthread_detach(thread);
    }
    return NULL;
}

static int start_http_server(int port, app_state *state, pthread_t *thread)
{
    struct sockaddr_in address;
    int reuse = 1;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }
    if (set_close_on_exec(listen_fd) != 0) {
        perror("fcntl FD_CLOEXEC");
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("bind");
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    if (listen(listen_fd, 16) != 0) {
        perror("listen");
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    if (pthread_create(thread, NULL, http_server, state) != 0) {
        perror("pthread_create http_server");
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    int exit_code = EXIT_FAILURE;
    int pipeline_started = 0;
    int server_started = 0;
    int color_encoder_started = 0;
    int depth_encoder_started = 0;
    int i;
    float depth_scale;
    uint8_t *color_rgb = NULL;
    uint8_t *depth_rgb = NULL;
    rs2_error *error = NULL;
    rs2_context *rs_context = NULL;
    rs2_device_list *devices = NULL;
    rs2_device *device = NULL;
    rs2_pipeline *pipeline = NULL;
    rs2_config *config = NULL;
    rs2_pipeline_profile *profile = NULL;
    app_state state;
    encoder color_encoder;
    encoder depth_encoder;
    pthread_t server_thread;
    struct timespec fps_start;
    unsigned long long fps_start_frames = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-p PORT]\n", argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            if (parse_port(argv[++i], &port) != 0) {
                fprintf(stderr, "Invalid port: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    memset(&state, 0, sizeof(state));
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.frame_ready, NULL);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    color_rgb = malloc(FRAME_BYTES);
    depth_rgb = malloc(FRAME_BYTES);
    if (color_rgb == NULL || depth_rgb == NULL) {
        fprintf(stderr, "Cannot allocate RGB frame buffers.\n");
        goto cleanup;
    }

    rs_context = rs2_create_context(RS2_API_VERSION, &error);
    if (report_rs_error("rs2_create_context", &error) != 0) {
        goto cleanup;
    }
    devices = rs2_query_devices(rs_context, &error);
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
    device = rs2_create_device(devices, 0, &error);
    if (report_rs_error("rs2_create_device", &error) != 0) {
        goto cleanup;
    }
    depth_scale = get_depth_scale(device);

    pipeline = rs2_create_pipeline(rs_context, &error);
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
                             FRAME_WIDTH,
                             FRAME_HEIGHT,
                             RS2_FORMAT_RGB8,
                             FRAME_FPS,
                             &error);
    if (report_rs_error("enable color stream", &error) != 0) {
        goto cleanup;
    }
    rs2_config_enable_stream(config,
                             RS2_STREAM_DEPTH,
                             0,
                             FRAME_WIDTH,
                             FRAME_HEIGHT,
                             RS2_FORMAT_Z16,
                             FRAME_FPS,
                             &error);
    if (report_rs_error("enable depth stream", &error) != 0) {
        goto cleanup;
    }
    profile = rs2_pipeline_start_with_config(pipeline, config, &error);
    if (report_rs_error("rs2_pipeline_start_with_config", &error) != 0) {
        fprintf(stderr, "The camera could not start RGB and depth at 640x480 60 Hz.\n");
        goto cleanup;
    }
    pipeline_started = 1;

    if (start_encoder(&color_encoder, &state, STREAM_COLOR, "color") != 0) {
        goto cleanup;
    }
    color_encoder_started = 1;
    if (start_encoder(&depth_encoder, &state, STREAM_DEPTH, "depth") != 0) {
        goto cleanup;
    }
    depth_encoder_started = 1;
    if (start_http_server(port, &state, &server_thread) != 0) {
        goto cleanup;
    }
    server_started = 1;

    printf("RealSense RGB + depth streaming at %dx%d @ %d Hz\n",
           FRAME_WIDTH,
           FRAME_HEIGHT,
           FRAME_FPS);
    printf("Depth scale: %.6f meter/unit; display range: 0-%.1f m\n",
           depth_scale,
           DEPTH_MAX_METERS);
    printf("Open http://<orange-pi-ip>:%d/ (Ctrl+C to stop)\n", port);
    fflush(stdout);
    clock_gettime(CLOCK_MONOTONIC, &fps_start);

    while (!stop_requested) {
        rs2_frame *frames = rs2_pipeline_wait_for_frames(pipeline, 5000, &error);
        int count;
        rs2_frame *color_frame = NULL;
        rs2_frame *depth_frame = NULL;
        const uint8_t *color_data = NULL;
        const uint16_t *depth_data = NULL;
        int depth_stride = 0;
        struct timespec now;
        double elapsed;

        if (report_rs_error("rs2_pipeline_wait_for_frames", &error) != 0) {
            break;
        }
        count = rs2_embedded_frames_count(frames, &error);
        if (report_rs_error("rs2_embedded_frames_count", &error) != 0) {
            rs2_release_frame(frames);
            break;
        }

        for (i = 0; i < count; ++i) {
            rs2_frame *frame = rs2_extract_frame(frames, i, &error);
            const rs2_stream_profile *stream_profile;
            rs2_stream stream;
            rs2_format format;
            int index;
            int unique_id;
            int framerate;
            if (report_rs_error("rs2_extract_frame", &error) != 0) {
                continue;
            }
            stream_profile = rs2_get_frame_stream_profile(frame, &error);
            if (report_rs_error("rs2_get_frame_stream_profile", &error) != 0) {
                rs2_release_frame(frame);
                continue;
            }
            rs2_get_stream_profile_data(stream_profile,
                                        &stream,
                                        &format,
                                        &index,
                                        &unique_id,
                                        &framerate,
                                        &error);
            if (report_rs_error("rs2_get_stream_profile_data", &error) != 0) {
                rs2_release_frame(frame);
                continue;
            }
            if (stream == RS2_STREAM_COLOR) {
                color_frame = frame;
            } else if (stream == RS2_STREAM_DEPTH) {
                depth_frame = frame;
            } else {
                rs2_release_frame(frame);
            }
        }

        if (color_frame != NULL && depth_frame != NULL) {
            color_data = rs2_get_frame_data(color_frame, &error);
            if (report_rs_error("color frame data", &error) == 0 &&
                copy_color_frame(color_frame, color_data, color_rgb, &error) == 0) {
                depth_data = rs2_get_frame_data(depth_frame, &error);
            }
            if (report_rs_error("frame data", &error) == 0 && depth_data != NULL) {
                depth_stride = rs2_get_frame_stride_in_bytes(depth_frame, &error);
            }
            if (report_rs_error("depth frame stride", &error) == 0 &&
                depth_stride > 0) {
                colorize_depth(depth_data, depth_stride, depth_scale, depth_rgb);
                queue_encoder_frame(&color_encoder, color_rgb);
                queue_encoder_frame(&depth_encoder, depth_rgb);

                pthread_mutex_lock(&state.mutex);
                ++state.captured_frames;
                pthread_mutex_unlock(&state.mutex);
                clock_gettime(CLOCK_MONOTONIC, &now);
                elapsed = (double)(now.tv_sec - fps_start.tv_sec) +
                          (double)(now.tv_nsec - fps_start.tv_nsec) / 1000000000.0;
                if (elapsed >= 1.0) {
                    pthread_mutex_lock(&state.mutex);
                    state.capture_fps =
                        (double)(state.captured_frames - fps_start_frames) / elapsed;
                    fps_start_frames = state.captured_frames;
                    pthread_mutex_unlock(&state.mutex);
                    fps_start = now;
                }
            }
        }

        if (color_frame != NULL) {
            rs2_release_frame(color_frame);
        }
        if (depth_frame != NULL) {
            rs2_release_frame(depth_frame);
        }
        rs2_release_frame(frames);
    }

    exit_code = stop_requested && !fatal_error ? EXIT_SUCCESS : EXIT_FAILURE;

cleanup:
    stop_requested = 1;
    pthread_mutex_lock(&state.mutex);
    pthread_cond_broadcast(&state.frame_ready);
    pthread_mutex_unlock(&state.mutex);
    if (listen_fd >= 0) {
        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
        listen_fd = -1;
    }
    if (server_started) {
        pthread_join(server_thread, NULL);
    }
    pthread_mutex_lock(&state.mutex);
    while (state.active_connections > 0) {
        pthread_cond_wait(&state.frame_ready, &state.mutex);
    }
    pthread_mutex_unlock(&state.mutex);
    if (color_encoder_started) {
        stop_encoder(&color_encoder);
    }
    if (depth_encoder_started) {
        stop_encoder(&depth_encoder);
    }
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
    if (device != NULL) {
        rs2_delete_device(device);
    }
    if (devices != NULL) {
        rs2_delete_device_list(devices);
    }
    if (rs_context != NULL) {
        rs2_delete_context(rs_context);
    }
    free(color_rgb);
    free(depth_rgb);
    free(state.jpeg[STREAM_COLOR]);
    free(state.jpeg[STREAM_DEPTH]);
    pthread_cond_destroy(&state.frame_ready);
    pthread_mutex_destroy(&state.mutex);
    return exit_code;
}

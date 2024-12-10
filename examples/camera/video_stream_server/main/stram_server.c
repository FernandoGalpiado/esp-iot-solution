/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_http_server.h"
#include "img_converters.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "esp_ota_ops.h"

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";
static QueueHandle_t xQueueFrameI = NULL;
static bool gReturnFB = true;
static httpd_handle_t stream_httpd = NULL;
#include <sys/param.h>
#include "esp_vfs.h"

static const char *TAG = "stream_s";

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE  1024*16

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
{
    char entrypath[FILE_PATH_MAX];

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

//    if (!dir) {
//        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
//        /* Respond with 404 Not Found */
//        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
//        flag_use_server = 0;
//        return ESP_FAIL;
//    }

    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

    /* Get handle to embedded file upload script */
    extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

    /* Send file-list table definition and column labels */
    httpd_resp_sendstr_chunk(req,
                             "<table class=\"fixed\" border=\"1\">"
                             "<col width=\"800px\" /><col width=\"300px\" /><col width=\"300px\" /><col width=\"100px\" />"
                             "<thead><tr><th>Name</th><th>Type</th><th>Size (Bytes)</th><th>Delete</th></tr></thead>"
                             "<tbody>");


    /* Finish the file list table */
    httpd_resp_sendstr_chunk(req, "</tbody></table>");

    /* Send remaining chunk of HTML file to complete it */
    httpd_resp_sendstr_chunk(req, "</body></html>");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
//    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
                                             
                                                     /* If name has trailing '/', respond with directory contents */
    if (strcmp(filename, "/ota") == 0) {
        return http_resp_dir_html(req, filepath);
    }
    else if (strcmp(filename, "/stream") == 0) {
        
    }
    else
    {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error");
	}
    
        /* If name has trailing '/', respond with directory contents */
//    if (filename[strlen(filename) - 1] == '/ota) {
//        return http_resp_dir_html(req, filepath);
//    }
    
    
	
	
    camera_fb_t *frame = NULL;
    struct timeval _timestamp;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];

//    *      httpd_resp_set_status() - for setting the HTTP status string,
//    *      httpd_resp_set_type()   - for setting the Content Type,
//    *      httpd_resp_set_hdr()    - for appending any additional field
//    *                                value entries in the response header

//    httpd_resp_set_status(req, HTTPD_200);
    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
//    res = httpd_resp_set_type(req, "picture");
    if (res != ESP_OK) {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    while (true) {
        if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY)) {
            _timestamp.tv_sec = frame->timestamp.tv_sec;
            _timestamp.tv_usec = frame->timestamp.tv_usec;

            if (frame->format == PIXFORMAT_JPEG) {
                _jpg_buf = frame->buf;
                _jpg_buf_len = frame->len;
            } else if (!frame2jpg(frame, 60, &_jpg_buf, &_jpg_buf_len)) {
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
            }
        } else {
            res = ESP_FAIL;
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
            if (res == ESP_OK) {

//            	ESP_LOGI(TAG, "boundary");
            	size_t lenbuf = 128;
            	memset(part_buf, 0, lenbuf);
                size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
                res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
//                httpd_resp_set_hdr(req, part_buf, "60");

            }
            if (res == ESP_OK) {
//            	ESP_LOGI(TAG, "ESP_OK boundary");
                res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
            }

            if (frame->format != PIXFORMAT_JPEG) {
                free(_jpg_buf);
                _jpg_buf = NULL;
            }
        }

        if (gReturnFB) {
            esp_camera_fb_return(frame);
        } else {
            free(frame->buf);
        }

        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Break stream handler");
            break;
        }
    }

    return res;
}

/* Handler to download a file kept on the server */
static esp_err_t new_ota_handler(httpd_req_t *req) {
//	char buf[1000];
char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
//received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE)))
	esp_ota_handle_t ota_handle;
	int remaining = req->content_len;
	vTaskDelay(20 / portTICK_PERIOD_MS);
	printf("OTA bytes: %d", remaining);

	const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
//	printf("\n\ntest1");
	vTaskDelay(1 / portTICK_PERIOD_MS);
	
	ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));
//	printf("\n\ntest2");

	while (remaining > 0) {
		vTaskDelay(1 / portTICK_PERIOD_MS);
		int recv_len = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE));

		// Timeout Error: Just retry
		if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;

		// Serious Error: Abort OTA
		} else if (recv_len <= 0) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
			return ESP_FAIL;
		}

		// Successful Upload: Flash firmware chunk
		if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
			return ESP_FAIL;
		}
//		printf("\n\ntest3 %d", recv_len);

		remaining -= recv_len;
	}

	// Validate and switch to new OTA image and reboot
	if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
			return ESP_FAIL;
	}

	printf("\n\ncomplete ");
	httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");

	vTaskDelay(1000 / portTICK_PERIOD_MS);
	esp_restart();

	return ESP_OK;
}

const char* identificarTipoArquivo(const char* nomeArquivo) {
    // Obtem a extensão do arquivo
    const char* extensao = strrchr(nomeArquivo, '.');

    // Verifica se a extensão é válida
    if (!extensao || extensao == nomeArquivo) {
        return "Extensão inválida";
    }

    // Compara a extensão e retorna o tipo de arquivo
    if (strcmp(extensao, ".mp4") == 0 || strcmp(extensao, ".avi") == 0 || strcmp(extensao, ".mkv") == 0) {
        return "Video";
    } else if (strcmp(extensao, ".jpg") == 0 || strcmp(extensao, ".jpeg") == 0 || strcmp(extensao, ".png") == 0) {
        return "Foto";
    } else if (strcmp(extensao, ".bin") == 0) {
        return "OTA";
    } else if (strcmp(extensao, ".txt") == 0 || strcmp(extensao, ".doc") == 0 || strcmp(extensao, ".pdf") == 0) {
        return "Texto";
    } else {
        return "Tipo desconhecido";
    }
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
	vTaskDelay(1 / portTICK_PERIOD_MS);
    char filepath[FILE_PATH_MAX];

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }
    printf("\n%s\n", filename);
    
//    identificarTipoArquivo(filename);
    /* If name has trailing '/', respond with directory contents */
    if (strcmp(identificarTipoArquivo(filename), "OTA") == 0) {
        return new_ota_handler(req);
    }
    else
    {
		ESP_LOGE(TAG, "File write failed!");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
        return ESP_FAIL;
	}
}

const char base_path[] = "ota";

esp_err_t start_stream_server(const QueueHandle_t frame_i, const bool return_fb)
{
	    static struct file_server_data *server_data = NULL;

    if (server_data) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

//    httpd_handle_t server = NULL;
//    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
//    config.uri_match_fn = httpd_uri_match_wildcard;

//    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
//    if (httpd_start(&server, &config) != ESP_OK) {
//        ESP_LOGE(TAG, "Failed to start file server!");
//        return ESP_FAIL;
//    }
	
	
    xQueueFrameI = frame_i;
    gReturnFB = return_fb;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 5120;
    httpd_uri_t stream_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = server_data
    };
    
        /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri       = "/upload/*",   // Match all URIs of type /upload/path/to/file
        .method    = HTTP_POST,
        .handler   = upload_post_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    

    esp_err_t err = httpd_start(&stream_httpd, &config);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(stream_httpd, &stream_uri);
        httpd_register_uri_handler(stream_httpd, &file_upload);
        ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
        return err;
    }
    ESP_LOGE(TAG, "httpd start err = %s", esp_err_to_name(err));
    return ESP_FAIL;
}

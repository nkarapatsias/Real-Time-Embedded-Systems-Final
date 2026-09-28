#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <libwebsockets.h>
#include <cjson/cJSON.h>

// Definitions of helping variables
#define BUFFER_SIZE  1024   // Circular buffer size
#define MAX_MSG_LEN 16384      // Maximum safe length of message receive (16KB)

int max_count = 0;

// Declaration of circular buffer 
typedef struct {
    char data[MAX_MSG_LEN];
} msg_t;

msg_t ring_buffer[BUFFER_SIZE];
int head = 0;
int tail = 0;
int count = 0;

struct lws *wsi;

// Global "kind" counters
unsigned long cnt_commit = 0;
unsigned long cnt_identity = 0;
unsigned long cnt_account = 0;
unsigned long cnt_info = 0;

volatile bool running = true; // For future expanding, but setting it to false can safely terminate the program
volatile bool connected = true; // Showing real time if connection with the server is established (For internet connection failures recognization)
volatile bool reconnecting = false; // True if there is a pending connection attempt 

// Mutex protection variables
pthread_mutex_t ring_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ring_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

  

// ----------- Producer thread ----------- //

// Callback to handle received data and put them in the ring buffer
static int data_handler_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    // Local data structures for the data_handler_callback
    static char local_data[MAX_MSG_LEN]; // Local buffer to receive data and pass them to ring buffer
    static size_t rx_len = 0; // Monitors the local_data occupancy
    
    switch (reason) {

        // Case where connection with the server is established
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Producer: Connected to Jetstream WebSocket successfully.\n");
            connected = true;
            reconnecting = false;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE: {
            size_t chunk = len; // Length of the incoming data
            
            // Ensure no buffer overflow happens by dropping the remaining bytes. The most correct way would be to store the dropped data and merge them afterwards
            // although in that project we just choose a fairly large MAX_MSG_LEN hence a lot of space for local_data to avoid overflowing. Note that we expect
            // messeges from 1 to 4 KB
            if (rx_len + chunk > MAX_MSG_LEN - 1) {
                chunk = MAX_MSG_LEN - 1 - rx_len;
            }
            memcpy(local_data + rx_len, in, chunk); // Copying the received data from the in argument to our local local_data
            rx_len += chunk; // Update the 

            // Check if received the last data of the message
            if (lws_is_final_fragment(wsi)) {
                local_data[rx_len] = '\0'; // Add null terminator

                pthread_mutex_lock(&ring_mutex); // Locking mutex to prevent accesses to ring buffer from other threads 

                // Ensure ring buffer have free space for our data
                if (count < BUFFER_SIZE) {
                    strncpy(ring_buffer[head].data, local_data, MAX_MSG_LEN); // Pass data from local local_data to global ring buffer
                    head = (head + 1) % BUFFER_SIZE; // Calculate head pointer
                    count++; 
                    if (count > max_count) {
                        max_count = count; // Track the peak
                    }
                    pthread_cond_signal(&ring_cond); // Waking up the consumer
                }
                pthread_mutex_unlock(&ring_mutex); // Unlock ring buffer mutex

                rx_len = 0; // Released the data of local_data
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Producer: Connection error!\n");
            connected = false;   
            rx_len = 0;    
            reconnecting = false;
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("Producer: Connection closed!\n");
            connected = false;   
            rx_len = 0;    
            reconnecting = false;
            break;
        default:
            break;
    }
    return 0;
}


static struct lws_protocols protocols[] = {
    { "jetstream-protocol", data_handler_callback, 0, MAX_MSG_LEN, },
    { NULL, NULL, 0, 0 }
};

// Main thread
void* producer_thread(void* arg) {
    struct lws_context_creation_info info; // LWS context configuration structure 
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_DISABLE_IPV6; 

    struct lws_context *context = lws_create_context(&info); 
    
    
    if (!context) {
        fprintf(stderr, "lws_create_context failed\n"); // If context creation failed, inform the user and quit the thread
        return NULL;
    }


    struct lws_client_connect_info ccinfo; // Connection confituration structure
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = context;
    ccinfo.address = "jetstream1.us-east.bsky.network";
    ccinfo.port = 443;
    ccinfo.path = "/subscribe?wantedCollections=app.bsky.feed.post";
    ccinfo.host = "jetstream1.us-east.bsky.network";
    ccinfo.origin = "origin";
    ccinfo.protocol = protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL; 

    // Initializing the asynchronus client connection to the websocket using wsi pointer
    wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi) {
        fprintf(stderr, "lws client connect failed\n"); // Verify connection attempt succeeded
        lws_context_destroy(context);
        return NULL;
    }
    
    
    // As long as our service is running we endlessly poll the server 
   time_t last_retry_time = time(NULL);

    while (running) {
        lws_service(context, 0); 
        if (!connected && !reconnecting) {
            time_t now = time(NULL);

            if (now - last_retry_time >= 3) {
                last_retry_time = now;
                reconnecting = true;
                printf("Attempting to reconnect to Jetstream...\n");
                struct lws *new_wsi = lws_client_connect_via_info(&ccinfo);

                if (!new_wsi) {
                    reconnecting = false;
                    fprintf(stderr, "Reconnect request failed to queue.\n");
                } else {
                    wsi = new_wsi;
                }
            }
        }
    }

    

    // Case where we need to end our service
    lws_context_destroy(context);

    pthread_mutex_lock(&ring_mutex); 
    pthread_cond_broadcast(&ring_cond); 
    pthread_mutex_unlock(&ring_mutex);

    return NULL;
}




// ----------- Consumer thread ----------- //
void* consumer_thread(void* arg) {
    char local_data[MAX_MSG_LEN]; // Local data storage 

    // Run consumer loop if the connection with the remote server is established
    while (running) {
        pthread_mutex_lock(&ring_mutex);
        // Goes to sleep if no new data are imported in the circular buffer
        while (count == 0 && running) {
            pthread_cond_wait(&ring_cond, &ring_mutex); // ring mutex released automatically while sleeping
        }

        // If the app is terminated firstly we empty the ring buffer and then we unlock the mutex and end the consumer thread
        if (!running && count == 0) {
            pthread_mutex_unlock(&ring_mutex);
            break;
        }

        // Fetch new data from ring_buffer to our local one and adjust the ring buffer state
        strncpy(local_data, ring_buffer[tail].data, MAX_MSG_LEN); 
        tail = (tail + 1) % BUFFER_SIZE;    
        count--;
        pthread_mutex_unlock(&ring_mutex);

        // Parsing the json info to recognize the kind field and increase the global counters
        cJSON *json = cJSON_Parse(local_data);
        if (json != NULL) {
            cJSON *kind = cJSON_GetObjectItemCaseSensitive(json, "kind");
            if (cJSON_IsString(kind) && (kind->valuestring != NULL)) {
                
                // Lock the counter's mutex and update their value safely
                pthread_mutex_lock(&counter_mutex);
                if (strcmp(kind->valuestring, "commit") == 0) {
                    cnt_commit++;
                } else if (strcmp(kind->valuestring, "identity") == 0) {
                    cnt_identity++;
                } else if (strcmp(kind->valuestring, "account") == 0) {
                    cnt_account++;
                } else {
                    cnt_info++;
                }
                pthread_mutex_unlock(&counter_mutex);
            }
            cJSON_Delete(json);
        }
    }
    return NULL;
}

// ----------- Monitoring thread ----------- //
// Helping function to measure cpu usage percentage
void get_cpu_times(unsigned long long *idle_time, unsigned long long *total_time) {
    FILE *fp = fopen("/proc/stat", "r"); // File given by the lecturer
    if (!fp) return;
    
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp)) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        
        *idle_time = idle + iowait;
        *total_time = user + nice + system + idle + iowait + irq + softirq + steal;
    }
    fclose(fp);
}


// Main thread
void* monitor_thread(void* arg) {
    // Creating the metrics_log.txt file 
    FILE *log_fp = fopen("metrics_log.txt", "a");
    if (!log_fp) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    // Adding the header of the metrics_log.txt 
    fprintf(log_fp, "Seconds,Nanoseconds,Commit_Count,Identity_Count,Account_Count,Info_Count,Buffer_Occupancy_Pct,CPU_Pct\n");
    fflush(log_fp);
    
    unsigned long long prev_idle = 0, prev_total = 0;
    get_cpu_times(&prev_idle, &prev_total);

    struct timespec next_wake;
    clock_gettime(CLOCK_MONOTONIC, &next_wake);

    while (running) {
        
        // Sleeping precisly every 1 second 
        next_wake.tv_sec += 1;
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_wake, NULL);

        // Reiciving the current time for the statistics timestamp 
        struct timespec ts_real;
        clock_gettime(CLOCK_REALTIME, &ts_real);
        
        // Calculating cpu's usage percentage even when there is no connection to the server
        unsigned long long cur_idle = 0, cur_total = 0;
        get_cpu_times(&cur_idle, &cur_total);
        
        unsigned long long diff_idle = cur_idle - prev_idle;
        unsigned long long diff_total = cur_total - prev_total;
        
        float cpu_pct = 0.0f;
        if (diff_total > 0) {
            cpu_pct = ((float)(diff_total - diff_idle) / (float)diff_total) * 100.0f;
        }
        
        prev_idle = cur_idle;
        prev_total = cur_total;

        // Normal operation when internet connection is established
        if(connected){
            // Reading counters, store them locally and reset the global ones to zero mutex protected to avoid conflicts with consumer
            pthread_mutex_lock(&counter_mutex);
            unsigned long c_com = cnt_commit;
            unsigned long c_id = cnt_identity;
            unsigned long c_acc = cnt_account;
            unsigned long c_inf = cnt_info;
            
            cnt_commit = 0;
            cnt_identity = 0;
            cnt_account = 0;
            cnt_info = 0;
            pthread_mutex_unlock(&counter_mutex);

            // Ring buffer occupancy calculation
            pthread_mutex_lock(&ring_mutex);
            int peak_count = max_count;
            max_count = count; // Reset peak to current baseline for the next second
            pthread_mutex_unlock(&ring_mutex);

            float occ_pct = ((float)peak_count / (float)BUFFER_SIZE) * 100.0f;



            // Write all the received data to metrics_log.txt 
            fprintf(log_fp, "%lld,%ld,%lu,%lu,%lu,%lu,%.2f,%.2f\n",(long long)ts_real.tv_sec, ts_real.tv_nsec, c_com, c_id, c_acc, c_inf, occ_pct, cpu_pct);
            fflush(log_fp);
        }
        // Opetation where connection with the server failed
        else
        {
            fprintf(log_fp, "%lld,%ld, NC, NC, NC, NC, NC, %.2f\n",(long long)ts_real.tv_sec, ts_real.tv_nsec, cpu_pct);
            fflush(log_fp);            
        }

    }
    
    fclose(log_fp);
    return NULL;
}




// Main function
int main() {
    pthread_t t_producer, t_consumer, t_monitor;

    printf("Starting Jetstream Monitor...\n");

    // Creating the 3 threads: Producer, Consumer, and Monitor
    if (pthread_create(&t_producer, NULL, producer_thread, NULL) != 0) {
        perror("Failed to create producer thread");
        return EXIT_FAILURE;
    }
    
    if (pthread_create(&t_consumer, NULL, consumer_thread, NULL) != 0) {
        perror("Failed to create consumer thread");
        return EXIT_FAILURE;
    }
    
    if (pthread_create(&t_monitor, NULL, monitor_thread, NULL) != 0) {
        perror("Failed to create monitor thread");
        return EXIT_FAILURE;
    }

    // Waiting for all threads to return. This happens only when the global variable "running" is set to false
    pthread_join(t_producer, NULL);
    pthread_join(t_consumer, NULL);
    pthread_join(t_monitor, NULL);

    printf("Monitor exited cleanly.\n");
    return EXIT_SUCCESS;
}
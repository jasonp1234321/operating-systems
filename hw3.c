#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <stdbool.h>

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char **words;

const char *global_dict_file = NULL;
char **full_dict = NULL;
int full_dict_size = 0;

pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t main_thread_id;

#define MAX_WORD_LEN 5

volatile sig_atomic_t shutdown_requested = 0;

void handle_sigusr1(int sig) {
    shutdown_requested = 1;
}

void to_lowercase(char *str) {
    while (*str) {
        *str = tolower(*str);
        str++;
    }
}

int is_valid_word(const char *guess) {
    for (int i = 0; i < full_dict_size; i++) {
        if (strcasecmp(guess, *(full_dict + i)) == 0) return 1;
    }
    return 0;
}

void encode_result(const char *guess, const char *word, char *result) {
    int *used_word = calloc(MAX_WORD_LEN, sizeof(int));
    int *used_guess = calloc(MAX_WORD_LEN, sizeof(int));

    for (int i = 0; i < MAX_WORD_LEN; i++) {
        if (tolower(guess[i]) == tolower(word[i])) {
            result[i] = toupper(guess[i]);
            used_word[i] = 1;
            used_guess[i] = 1;
        } else {
            result[i] = '-';
        }
    }

    for (int i = 0; i < MAX_WORD_LEN; i++) {
        if (used_guess[i]) continue;
        for (int j = 0; j < MAX_WORD_LEN; j++) {
            if (!used_word[j] && tolower(guess[i]) == tolower(word[j])) {
                result[i] = tolower(guess[i]);
                used_word[j] = 1;
                break;
            }
        }
    }

    result[MAX_WORD_LEN] = '\0';
    free(used_word);
    free(used_guess);
}

void send_packet(int fd, char valid, int guesses_left, const char *result) {
    char packet[8];
    packet[0] = valid;

    short net_guesses = htons((short)guesses_left);
    memcpy(packet + 1, &net_guesses, sizeof(short));

    for (int i = 0; i < MAX_WORD_LEN; i++) {
        packet[3 + i] = result[i];
    }

    write(fd, packet, 8);
}

void send_guess_packet(int fd, const char *input_line, char *out_guess) {
    char packet[5];
    int i = 0;
    while (i < 5 && *(input_line + i) != '\n' && *(input_line + i) != '\0') {
        *(packet + i) = *(input_line + i);
        i++;
    }
    while (i < 5) {
        *(packet + i) = ' '; 
        i++;
    }

    memcpy(out_guess, packet, 5);
    out_guess[5] = '\0';
    write(fd, packet, 5);
}


struct client_info {
    int client_fd;
    char *target_word;
};

void *client_thread(void *arg) {
    struct client_info *info = (struct client_info *)arg;
    int client_fd = info->client_fd;
    char *target_word = info->target_word;
    free(info);

    char guess[MAX_WORD_LEN + 1] = {0};
    char result[MAX_WORD_LEN + 1] = {0};
    char line[100];
    char raw_guess[MAX_WORD_LEN + 1] = {0};

    int guesses_used = 0;
    bool won = false;

    FILE *client_stream = fdopen(client_fd, "r+");
    if (!client_stream) {
        perror("fdopen() failed");
        close(client_fd);
        free(target_word);
        pthread_exit(NULL);
    }

    printf("THREAD %lu: waiting for guess\n", pthread_self());
    dprintf(client_fd, "CLIENT: connecting to server...\n");

    while (1) {
        fprintf(client_stream, "CLIENT: sending to server: "); 
        fflush(client_stream);

        if (!fgets(line, sizeof(line), client_stream)) break;
        *(line + strcspn(line, "\n")) = '\0';

        send_guess_packet(client_fd, line, raw_guess);

        if (strlen(line) != MAX_WORD_LEN || !is_valid_word(line)) {
            snprintf(result, MAX_WORD_LEN + 1, "?????");
            send_packet(client_fd, 'N', 6 - guesses_used, result);
            dprintf(client_fd, "CLIENT: invalid guess -- %d guesses remaining\n", 6 - guesses_used);
            printf("THREAD %lu: invalid guess; sending reply: ????? (%d guesses left)\n", pthread_self(), 6 - guesses_used);
        } else {
            strncpy(guess, raw_guess, MAX_WORD_LEN);
            guess[MAX_WORD_LEN] = '\0';
            to_lowercase(guess);

            encode_result(guess, target_word, result);
            guesses_used++;

            fflush(client_stream);
            send_packet(client_fd, 'Y', 6 - guesses_used, result);

            char *visible = malloc(6);
            memcpy(visible, result, 5);
            *(visible + 5) = '\0';

            printf("THREAD %lu: rcvd guess: %s\n", pthread_self(), raw_guess);
            printf("THREAD %lu: sending reply: %s (%d guesses left)\n", pthread_self(), result, 6 - guesses_used);


            fflush(client_stream);
            dprintf(client_fd, "CLIENT: response: %s -- %d guesses remaining\n", visible, 6 - guesses_used);

            free(visible);

            if (strcmp(guess, target_word) == 0) {
                won = true;
                break;
            }

            if (guesses_used >= 6) break;
        }
    }

    pthread_mutex_lock(&stats_mutex);
    int count = 0;
    while (words && words[count]) count++;
    char **new_words = realloc(words, sizeof(char *) * (count + 2));
    if (new_words) {
        words = new_words;
        words[count] = strdup(target_word);
        words[count + 1] = NULL;
    }

    if (won) {
        total_wins++;
        total_guesses += guesses_used;
        dprintf(client_fd, "CLIENT: you won!\n");
    } else {
        total_losses++;
        total_guesses += guesses_used;
        dprintf(client_fd, "CLIENT: game over!\n");
    }

    dprintf(client_fd, "CLIENT: disconnecting...\n");
    pthread_mutex_unlock(&stats_mutex);

    printf("THREAD %lu: game over; word was %s!\n", pthread_self(), target_word);
    pthread_kill(main_thread_id, SIGUSR1);

    fclose(client_stream);
    close(client_fd);
    free(target_word);
    pthread_exit(NULL);
}




int wordle_server(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "ERROR: Invalid argument(s)\n");
        fprintf(stderr, "USAGE: hw3.out <listener-port> <seed> <dictionary-filename> <num-words>\n");
        return EXIT_FAILURE;
    }

    main_thread_id = pthread_self();

    int port = atoi(argv[1]);
    int seed = atoi(argv[2]);
    global_dict_file = argv[3];
    int num_words = atoi(argv[4]);

    FILE *fp = fopen(global_dict_file, "r");
    if (!fp) {
        perror("fopen() failed");
        return EXIT_FAILURE;
    }

    full_dict = calloc(num_words, sizeof(char *));
    if (!full_dict) return EXIT_FAILURE;

    char line[100];
    while (fgets(line, sizeof(line), fp) && full_dict_size < num_words) {
        line[strcspn(line, "\n")] = '\0';
        full_dict[full_dict_size++] = strdup(line);
    }
    fclose(fp);

    printf("MAIN: opened %s (%d words)\n", global_dict_file, full_dict_size);
    srand(seed);
    printf("MAIN: seeded pseudo-random number generator with %d\n", seed);

    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(server_fd, 10) < 0) {
        perror("bind/listen failed");
        return EXIT_FAILURE;
    }

    printf("MAIN: Wordle server listening on port {%d}\n", port);

    while (!shutdown_requested) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept() failed");
            break;
        }

        printf("MAIN: rcvd incoming connection request\n");
        int idx = rand() % full_dict_size;
        struct client_info *info = calloc(1, sizeof(struct client_info));
        info->client_fd = client_fd;
        info->target_word = strdup(full_dict[idx]);

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, info) != 0) {
            perror("pthread_create() failed");
            close(client_fd);
            free(info->target_word);
            free(info);
        } else {
            pthread_detach(tid);
        }
    }

    printf("MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");
    close(server_fd);
    for (int i = 0; i < full_dict_size; i++) free(full_dict[i]);
    free(full_dict);
    return EXIT_SUCCESS;
}

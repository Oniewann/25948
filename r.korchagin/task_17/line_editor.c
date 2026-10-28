#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <signal.h>
#include <poll.h>

#define MAX_LINE 40
#define MAX_HIST 100

#ifndef _POSIX_VDISABLE
#define _POSIX_VDISABLE 0
#endif

static struct termios orig_termios;

// Буферы для истории строк
static char history[MAX_HIST][MAX_LINE + 1];
static int hist_count = 0;
static int hist_idx = 0;
static char temp_buffer[MAX_LINE + 1];

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void sig_handler(int sig) {
    (void)sig;
    disable_raw_mode();
    _exit(EXIT_SUCCESS);
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    atexit(disable_raw_mode);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

static int is_erase_key(char c, cc_t verase) {
    return (c == (char)verase || c == 127 || c == '\b');
}

static int is_kill_key(char c, cc_t vkill) {
    return (c == (char)vkill || c == 21);
}

static int is_werase_key(char c, cc_t vwerase) {
    return (c == 23 || (vwerase != (cc_t)_POSIX_VDISABLE && c == (char)vwerase));
}

static int is_eof_key(char c, cc_t veof) {
    return (c == 4 || (veof != (cc_t)_POSIX_VDISABLE && c == (char)veof));
}

// Очистка и перерисовка строки с возвратом курсора на позицию cursor_pos
static void redraw_line(const char *line, int len, int cursor_pos) {
    printf("\r\033[K%s", line);
    for (int i = 0; i < len - cursor_pos; i++) {
        putchar('\b');
    }
    fflush(stdout);
}

static void add_to_history(const char *str) {
    if (hist_count < MAX_HIST) {
        strncpy(history[hist_count], str, MAX_LINE);
        history[hist_count][MAX_LINE] = '\0';
        hist_count++;
    } else {
        for (int i = 1; i < MAX_HIST; i++) {
            strcpy(history[i - 1], history[i]);
        }
        strncpy(history[MAX_HIST - 1], str, MAX_LINE);
        history[MAX_HIST - 1][MAX_LINE] = '\0';
    }
}

int main(void) {
    enable_raw_mode();

    cc_t verase = orig_termios.c_cc[VERASE];
    cc_t vkill = orig_termios.c_cc[VKILL];
    cc_t veof = orig_termios.c_cc[VEOF];
#ifdef VWERASE
    cc_t vwerase = orig_termios.c_cc[VWERASE];
#else
    cc_t vwerase = (cc_t)_POSIX_VDISABLE;
#endif

    char line[MAX_LINE + 1];
    int len = 0;
    int cursor_pos = 0;
    line[0] = '\0';
    temp_buffer[0] = '\0';

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        // Перехват стрелок через Escape-последовательности
        if (c == 27) {
            struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
            if (poll(&pfd, 1, 50) > 0) {
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) == 1 && (seq[0] == '[' || seq[0] == 'O')) {
                    if (poll(&pfd, 1, 50) > 0 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                        if (seq[1] == 'D') { // ВЛЕВО
                            if (cursor_pos > 0) {
                                cursor_pos--;
                                putchar('\b');
                                fflush(stdout);
                            } else {
                                putchar('\a');
                                fflush(stdout);
                            }
                            continue;
                        } else if (seq[1] == 'C') { // ВПРАВО
                            if (cursor_pos < len) {
                                printf("\033[C");
                                cursor_pos++;
                                fflush(stdout);
                            } else {
                                putchar('\a');
                                fflush(stdout);
                            }
                            continue;
                        } else if (seq[1] == 'A') { // ВВЕРХ (Предыдущая строка из истории)
                            if (hist_idx > 0) {
                                if (hist_idx == hist_count) {
                                    // Сохраняем текущий ввод перед началом навигации по истории
                                    memcpy(temp_buffer, line, len + 1);
                                }
                                hist_idx--;
                                strcpy(line, history[hist_idx]);
                                len = strlen(line);
                                cursor_pos = len;
                                redraw_line(line, len, cursor_pos);
                            } else {
                                putchar('\a'); // Достигнуто начало истории
                                fflush(stdout);
                            }
                            continue;
                        } else if (seq[1] == 'B') { // ВНИЗ (Следующая строка из истории)
                            if (hist_idx < hist_count) {
                                hist_idx++;
                                if (hist_idx == hist_count) {
                                    // Возврат к набранному тексту, который еще не был отправлен
                                    strcpy(line, temp_buffer);
                                } else {
                                    strcpy(line, history[hist_idx]);
                                }
                                len = strlen(line);
                                cursor_pos = len;
                                redraw_line(line, len, cursor_pos);
                            } else {
                                putchar('\a'); // Достигнут конец истории
                                fflush(stdout);
                            }
                            continue;
                        }
                    }
                }
            }
            putchar('\a');
            fflush(stdout);
            continue;
        }

        // 5. CTRL-D: выход, если курсор в начале пустой строки
        if (is_eof_key(c, veof)) {
            if (len == 0 && cursor_pos == 0) {
                printf("\r\nExiting...\r\n");
                fflush(stdout);
                break;
            } else {
                putchar('\a');
                fflush(stdout);
            }
            continue;
        }

        // Enter: сохранение в историю и переход на новую строку
        if (c == '\r' || c == '\n') {
            if (len > 0) {
                add_to_history(line);
            }
            hist_idx = hist_count;
            temp_buffer[0] = '\0';

            printf("\r\n");
            fflush(stdout);
            len = 0;
            cursor_pos = 0;
            line[0] = '\0';
            continue;
        }

        // 2. ERASE (Backspace): удаление символа перед курсором
        if (is_erase_key(c, verase)) {
            if (cursor_pos > 0) {
                memmove(&line[cursor_pos - 1], &line[cursor_pos], len - cursor_pos + 1);
                len--;
                cursor_pos--;
                redraw_line(line, len, cursor_pos);
            } else {
                putchar('\a');
                fflush(stdout);
            }
            continue;
        }

        // 3. KILL (Ctrl+U): очистка текущей строки
        if (is_kill_key(c, vkill)) {
            if (len > 0) {
                len = 0;
                cursor_pos = 0;
                line[0] = '\0';
                printf("\r\033[K");
                fflush(stdout);
            }
            continue;
        }

        // 4. CTRL-W: удаление слова слева от курсора
        if (is_werase_key(c, vwerase)) {
            if (cursor_pos == 0) {
                putchar('\a');
                fflush(stdout);
            } else {
                int i = cursor_pos;
                while (i > 0 && line[i - 1] == ' ') i--;
                while (i > 0 && line[i - 1] != ' ') i--;
                while (i > 0 && line[i - 1] == ' ') i--;

                int deleted = cursor_pos - i;
                memmove(&line[i], &line[cursor_pos], len - cursor_pos + 1);
                len -= deleted;
                cursor_pos = i;
                redraw_line(line, len, cursor_pos);
            }
            continue;
        }

        // 6. Непечатаемые символы
        if (!isprint((unsigned char)c)) {
            putchar('\a');
            fflush(stdout);
            continue;
        }

        // 1 & 7. Вставка символа и перенос на 40-й позиции
        if (len < MAX_LINE) {
            memmove(&line[cursor_pos + 1], &line[cursor_pos], len - cursor_pos + 1);
            line[cursor_pos] = c;
            len++;
            cursor_pos++;
            redraw_line(line, len, cursor_pos);
        } else {
            if (c == ' ') {
                if (len > 0) add_to_history(line);
                hist_idx = hist_count;
                printf("\r\n");
                fflush(stdout);
                len = 0;
                cursor_pos = 0;
                line[0] = '\0';
            } else {
                int word_start = MAX_LINE - 1;
                while (word_start >= 0 && line[word_start] != ' ') {
                    word_start--;
                }
                word_start++;

                int word_len = MAX_LINE - word_start;

                if (word_start == 0 || word_len == 0) {
                    if (len > 0) add_to_history(line);
                    hist_idx = hist_count;
                    printf("\r\n");
                    putchar(c);
                    fflush(stdout);
                    line[0] = c;
                    len = 1;
                    cursor_pos = 1;
                    line[1] = '\0';
                } else {
                    line[word_start] = '\0';
                    printf("\r\033[K%s\r\n", line);

                    char new_line[MAX_LINE + 1];
                    int new_len = 0;
                    for (int i = word_start; i < MAX_LINE; i++) {
                        new_line[new_len++] = line[i];
                    }
                    new_line[new_len++] = c;
                    new_line[new_len] = '\0';

                    memcpy(line, new_line, new_len + 1);
                    len = new_len;
                    cursor_pos = new_len;
                    printf("%s", line);
                    fflush(stdout);
                }
            }
        }
    }

    return EXIT_SUCCESS;
}

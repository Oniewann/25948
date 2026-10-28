#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <string.h>
#include <libgen.h>

static void print_file_info(const char *path) {
    struct stat st;

    // Используем lstat, чтобы не переходить по символическим ссылкам
    if (lstat(path, &st) == -1) {
        perror(path);
        return;
    }

    // 1. Определение типа файла (d, - или ?)
    char type_char = '?';
    if (S_ISDIR(st.st_mode)) {
        type_char = 'd';
    } else if (S_ISREG(st.st_mode)) {
        type_char = '-';
    } else {
        type_char = '?';
    }

    // 2. Права доступа (rwx для владельца, группы и остальных)
    char perms[10];
    perms[0] = (st.st_mode & S_IRUSR) ? 'r' : '-';
    perms[1] = (st.st_mode & S_IWUSR) ? 'w' : '-';
    perms[2] = (st.st_mode & S_IXUSR) ? 'x' : '-';
    perms[3] = (st.st_mode & S_IRGRP) ? 'r' : '-';
    perms[4] = (st.st_mode & S_IWGRP) ? 'w' : '-';
    perms[5] = (st.st_mode & S_IXGRP) ? 'x' : '-';
    perms[6] = (st.st_mode & S_IROTH) ? 'r' : '-';
    perms[7] = (st.st_mode & S_IWOTH) ? 'w' : '-';
    perms[8] = (st.st_mode & S_IXOTH) ? 'x' : '-';
    perms[9] = '\0';

    // 3. Имена владельца и группы
    struct passwd *pw = getpwuid(st.st_uid);
    struct group  *gr = getgrgid(st.st_gid);

    char owner[32];
    char group[32];

    if (pw != NULL) {
        snprintf(owner, sizeof(owner), "%s", pw->pw_name);
    } else {
        snprintf(owner, sizeof(owner), "%u", (unsigned int)st.st_uid);
    }

    if (gr != NULL) {
        snprintf(group, sizeof(group), "%s", gr->gr_name);
    } else {
        snprintf(group, sizeof(group), "%u", (unsigned int)st.st_gid);
    }

    // 4. Размер файла: выводится ТОЛЬКО для обычных файлов
    char size_str[16];
    if (S_ISREG(st.st_mode)) {
        snprintf(size_str, sizeof(size_str), "%lld", (long long)st.st_size);
    } else {
        size_str[0] = '\0'; // Пустое поле для каталогов, устройств и др.
    }

    // 5. Дата модификации (mtime)
    time_t now = time(NULL);
    time_t six_months_sec = 180 * 24 * 60 * 60;
    struct tm *lt = localtime(&st.st_mtime);
    char time_str[32];

    if (lt == NULL) {
        snprintf(time_str, sizeof(time_str), "???");
    } else {
        // Если файл старше 6 месяцев или дата в будущем — выводим год, иначе время
        if ((now - st.st_mtime > six_months_sec) || (st.st_mtime > now)) {
            strftime(time_str, sizeof(time_str), "%b %e  %Y", lt);
        } else {
            strftime(time_str, sizeof(time_str), "%b %e %H:%M", lt);
        }
    }

    // 6. Имя файла (только имя без пути)
    char *path_copy = strdup(path);
    char *base = basename(path_copy);

    // 7. Форматированный табличный вывод
    printf("%c%s %3ld %-8s %-8s %8s %s %s\n",
           type_char,
           perms,
           (long)st.st_nlink,
           owner,
           group,
           size_str,
           time_str,
           base);

    free(path_copy);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Использование: %s <файл1> [файл2 ...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++) {
        print_file_info(argv[i]);
    }

    return EXIT_SUCCESS;
}

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <utime.h>
#include <signal.h>

#define DEBUG 0

unsigned int smallFileTreshold = 100;
unsigned int interval = 5 * 60;
int recursive = 0;
int runBySignal = 0;

int isDir(const char *path);
void sync_dirs(char* src, char* dest);

char* sdir = NULL;
char* ddir = NULL;

// Spis:
// A | SIGUSR1
// B | Parametry programu
// C | Demon
// D | Synchronizacja
// E | Skanowanie
// F | Kopiowanie
// X | Debugowanie
// O | Inne komentarze (Niezwiazane ze soba)

// A | Po otrzymaniu SIGUSR1 ustawiamy, ze uruchomilismy przez sygnal
// A | Teoretycznie moglibysmy wykonac `sync_dirs(sdir, ddir)`, ale okazuje sie,
// A | ze SIGUSR1 przerywa sleepa w demonie.
void handler (int signum) {
    runBySignal = 1;
}

int main(int argc, char* argv[]) {
    // A | Otwieramy odpowiednio syslog i dodajemy handler na SIGUSR1
    openlog(argv[0], LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
    signal(SIGUSR1, handler);

    // B | Bierzemy odpowiednie opcje przez getopt, kod doslownie zajebany z zajec.
    // B |
    // B | Wymagane opcje:
    // B | -s       wymaga podania argumentu        sciezka src
    // B | -d       wymaga podania argumentu        sciezka dest
    // B |
    // B | Opcjonalne opcje:
    // B | -t       wymaga podania argumentu        treshold miedzy duzymi a malymi plikami (ilosc znakow, czy bajtow, idk)
    // B | -i       wymaga podania argumentu        interwal co ile sekund demon ma sie uruchamiac
    // B | -R                                       sprawdza rekurencyjnie
    int c;
    while ((c = getopt(argc, argv, "Rt:i:s:d:")) != -1) {
        uintmax_t num;

        switch (c) {
            case 's':
                sdir = optarg;

            case 'd':
                ddir = optarg;

            case 'R':
                recursive = 1;
                break;
            case 't':
                // B | Zamieniamy stringa na inta w poprawny dla jezyka c sposob
                num = strtoumax(optarg, NULL, 10);
                if (num != UINTMAX_MAX || errno != ERANGE) {
                    smallFileTreshold = num;
                }
                break;
            case 'i':
                // B | Zamieniamy stringa na inta w poprawny dla jezyka c sposob
                num = strtoumax(optarg, NULL, 10);
                if (num != UINTMAX_MAX || errno != ERANGE) {
                    interval = num;
                }
                break;

            case '?':
                // B | Wykonuje sie w momencie gdy np nie podalismy opcji jakiejs
                // B | Albo w momencie jakiegos innego bledu
                if (optopt == 't' || optopt == 'i') {
                    fprintf(stderr, "option -%c requires an argument. \n", optopt);
                    exit(EXIT_FAILURE);
                }

                if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                    exit(EXIT_FAILURE);
                }

                fprintf(stderr,"Unknown option character `\\x%x'.\n", optopt);
                exit(EXIT_FAILURE);

            default:
                // B | Idk, zajebane z zajec
                abort();
        }
    }

    // B | Sprawdzamy, czy sdir i ddir jest ustawiony, czyli czy mamy podane -s i -d
    if (sdir == NULL || ddir == NULL) {
        fprintf(stderr, "Usage: %s -s source_dir -d destination_dir [-R] [-t size_treshold] [-i daemon_interval]", argv[0]);
        exit(EXIT_FAILURE);
    }

    sdir = realpath(sdir, NULL);
    ddir = realpath(ddir, NULL);

    if (!isDir(sdir)) {
        fprintf(stderr, "Source path `%s` does not exist!\n", sdir);
        exit(EXIT_FAILURE);
    }

    if (!isDir(ddir)) {
        fprintf(stderr, "Source path `%s` does not exist!\n", ddir);
        exit(EXIT_FAILURE);
    }

    // X | Jezeli mamy flage DEBUG na 0, czyli w oddanym projekcie, to sie odpali jako demon
    // X | Natomiast jezeli jest jako 1 to sie uruchomi raz
#if DEBUG == 0
    // C | Kod zajebany z mojego rozwiazania z zajec
    // C | Ogolnie demony, ktore byly targetowane na sysvinit (takimi sie zajmujemy na zajeciach)
    // C | maja okreslone jak maja sie zachowywac, wiec masz wszystko wedlug standardow tu zrobione

    // C | Wpierw forkujemy by miec 2 oddzielne procesy
    pid_t pid = fork();
    if (pid == -1) {
        syslog(LOG_ERR, "Can't fork daemon");
        exit(EXIT_FAILURE);
    }

    // C | Z glownego procesu wychodzimy
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // C | Specka mowi, ze trzeba ustawic umask na 0
    umask(0);

    // C | Specka mowi, ze trzeba zrobic nowa sesje uzywajac setsid
    pid_t sid = setsid();
    if (sid < 0) {
        syslog(LOG_ERR, "Can't run ssid");
        exit(EXIT_FAILURE);
    }

    // C | Specka mowi, ze trzeba wyjsc do /
    if (chdir("/") == -1) {
        syslog(LOG_ERR, "Can't chdir into /");
        exit(EXIT_FAILURE);
    }

    // C | Specka mowi, ze trzeba zamknac wszystkie fd
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // C | No i lecimy w nieskonczonosc
    while (1) {
        // A | Jezeli runBySignal jest 1, to wpisujemy tu `signal`, w innym wypadku `daemon`
        // A | i resetujemy zmienna runBySignal
        syslog(LOG_NOTICE, "Sync daemon start (%s)", runBySignal ? "signal" : "daemon");
        runBySignal = 0;

        // D | Wywolujemy synchronizacje
        sync_dirs(sdir, ddir);

        syslog(LOG_NOTICE, "Sync daemon end");

        // C | Czekamy sobie <interval> sekund
        sleep(interval);
    }
#else
    // X | Uruchamiamy synchronizacje tylko raz w celu zdebugowania
    sync_dirs(sdir, ddir);
#endif
}

char* pathJoin (char* a, char* b) {
    // O | Funkcja do zrobienia z /home/waff i file.txt : /home/waff/file.txt
    // O | alokujemy miejsce na a + b + '/' + '\0'
    // O | ustawiamy na calej dlugosci stringa wszystkie wartosci na '\0'
    // O | Na koncu wrzucamy wpierw a, pozniej '/', pozniej b do stringa
    // O | Zwracamy stringa
    char* res = malloc(sizeof(char) * (strlen(a) + strlen(b) + 2));
    memset(res, '\0', strlen(a) + strlen(b) + 2);

    strcat(res, a);
    strcat(res, "/");
    strcat(res, b);
    return res;
}

char** scan(char* path) {
    struct dirent *entry;

    // E | Skanowanie
    // E | Tworzymy liste stringow o poczatkowej dlugosci 16;
    // E | Pozniej ja rozszerzamy dynamicznie
    int size = 16;
    char** res = malloc(size * sizeof(struct dirent*));
    for (int i = 0; i < size; i++) {
        res[i] = NULL;
    }

    DIR* dir = opendir(path);
    if (dir == NULL) {
        syslog(LOG_NOTICE, "scanning: %s \n", path);
        syslog(LOG_ERR, "Unable to read directory");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; (entry = readdir(dir)) != NULL;) {
        switch (entry->d_type) {
            case DT_REG:
                // E | Tworzymy nowego stringa i dopisujemy go do listy wynikowej
                res[i] = malloc(sizeof(char) * (strlen(entry->d_name) + 1));
                memset(res[i], '\0', strlen(entry->d_name) + 1);
                strcat(res[i++], entry->d_name);
                break;

            case DT_DIR:
                // E | Jezeli nie wspieramy rekurencji to nara
                if (!recursive) {
                    break;
                }

                // E | Filtrujemy aktualny i poprzedni katalog
                // E | Nie chcemy sobie systemu zjebac, nie?
                // E |
                // E | Jakby `.` zostala, to bysmy sie zapetlili
                // E | w nieskonczonosc sprawdzajac ./a/././././././
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    break;
                }

                // E | Dodajemy folder
                res[i] = malloc(sizeof(char) * (strlen(entry->d_name) + 1));
                memset(res[i], '\0', strlen(entry->d_name) + 1);
                strcat(res[i++], entry->d_name);

                // E | Dodalismy element, wiec jezeli jestesmy na koncu to rozszerzamy
                if (i + 1 >= size) {
                    res = realloc(res, size * 2 * sizeof(char*));
                    for (int j = 0; j < size; ++j) res[size + j] = NULL;
                    size *= 2;
                }

                // E | Bierzemy liste plikow i folderow dla danego folderu
                // E | Czyli skanujemy rekurencyjnie
                char** children = scan(pathJoin(path, entry->d_name));

                // E | Przepisujemy elementy po kolei z naszego
                // E | rekurencyjnego wywolania do aktualnego wyniku
                for (int j = 0; children[j] != NULL; ++j) {
                    char* file = pathJoin(entry->d_name, children[j]);
                    res[i] = malloc(sizeof(char) * (strlen(file) + 1));
                    memset(res[i], '\0', strlen(file) + 1);
                    strcat(res[i++], file);

                    // E | Dodalismy element, wiec jezeli jestesmy na koncu to rozszerzamy
                    if (i + 1 >= size) {
                        res = realloc(res, size * 2 * sizeof(char*));
                        for (int j = 0; j < size; ++j) res[size + j] = NULL;
                        size *= 2;
                    }
                }

                break;
        }

        // E | Dodalismy element, wiec jezeli jestesmy na koncu to rozszerzamy
        if (i + 1 >= size) {
            res = realloc(res, size * 2 * sizeof(char*));
            for (int j = 0; j < size; ++j) res[size + j] = NULL;
            size *= 2;
        }
    }

    closedir(dir);
    return res;
}

// F | Kopiujemy plik src do dest
// F | Dodatkowo pobieram wczesniej pobrany stat pliku src
// F | By nie pobierac go 2 razy
int copy(char *src, char *dest, struct stat stat) {
    int srcFd = open(src, O_RDONLY);
    int destFd = open(dest, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP | S_IWGRP);
    if (srcFd < 0 || destFd < 0) return -1;

    // F | Jezeli rozmiar == 0 (mmap nie umie w rozmiar 0) lub gdy jestesmy ponizej tresholdu
    if (stat.st_size == 0 || stat.st_size < smallFileTreshold) {
        char buf[stat.st_size];

        // F | Wczytujemy do buf i zapisujemy do destFd z buf
        // F | Wykorzystujemy to, ze warunki w ifie ida od lewej do prawej;
        // F | Jezeli lewy warunek jest spelniony, to nie wykonujemy tego po prawej
        if (read(srcFd, buf, stat.st_size) < 0 || write(destFd, buf, stat.st_size) < 0) return -1;
    } else {
        // F | No to ustawiamy buf by byl zmapowany na cala tresc pliku src
        char* buf = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);

        // F | Ten sam myk co wyzej
        // F | Jezeli mmap zajebal failem to -1
        // F | Jezeli nie, to write
        if ((stat.st_size != 0 && buf == MAP_FAILED) || write(destFd, buf, stat.st_size) < 0) return -1;
    }

    close(srcFd);
    close(destFd);

    // F | Ustawiamy czas modyfikacji pliku dest na taki sam jak byl pliku src
    // F | Wysrane na bledy
    struct utimbuf newTimes;
    newTimes.modtime = stat.st_mtim.tv_sec;
    newTimes.actime = stat.st_atim.tv_sec;
    utime(dest, &newTimes);
    return 0;
}

// D | Synchronizacja
void sync_dirs(char* src, char* dest) {
    char** srcFiles = scan(src);
    char** destFiles = scan(dest);

    // D | Iterujemy po plikach w src
    for (int i = 0; srcFiles[i] != NULL; ++i) {
        // D | Bierzemy info o pliku src - pathJoin(SRC, ...)
        struct stat srcStat;
        if (stat(pathJoin(src, srcFiles[i]), &srcStat) != 0) {
            syslog(LOG_ERR, "Cannot stat source file");
            exit(EXIT_FAILURE);
        }

        struct stat destStat;
        // D | Bierzemy info o pliku dest - pathJoin(DEST, ...)
        if (stat(pathJoin(dest, srcFiles[i]), &destStat) != 0) {
            // D | Aktualnie jestesmy w scope, ktory mowi o tym, ze napotkalismy blad - stat(...) != 0
            // D | Jezeli blad to nie ENOENT (czyli brak pliku/katalogu) to wypierdalamy
            // D | Jezeli blad to ENOENT to tworzymy plik / katalog
            if (errno != ENOENT) {
                syslog(LOG_ERR, "Cannot stat destination file");
                exit(EXIT_FAILURE);
            }

            // D | Jezeli plik to katalog - tworzymy katalog
            if (S_ISDIR(srcStat.st_mode)) {
                syslog(LOG_NOTICE, "create directory %s\n", pathJoin(dest, srcFiles[i]));
                // D | Tutaj uzywamy MKDIR
                if (mkdir(pathJoin(dest, srcFiles[i]), S_IRWXU) != 0) {
                    syslog(LOG_ERR, "create directory %s failed\n", pathJoin(dest, srcFiles[i]));
                }
                continue;
            }

            // D | Jezeli plik to plik - kopiujemy plik
            syslog(LOG_NOTICE, "copy %s to %s\n", pathJoin(src, srcFiles[i]), pathJoin(dest, srcFiles[i]));
            if (copy(pathJoin(src, srcFiles[i]), pathJoin(dest, srcFiles[i]), srcStat) != 0) {
                syslog(LOG_ERR, "copy %s to %s failed\n", pathJoin(src, srcFiles[i]), pathJoin(dest, srcFiles[i]));
            }
            continue;
        }

        // D | Teoretycznie mozna tez dodac tutaj check odnosnie tego czy
        // D | isdir(src) != isdir(dest), ale zapewne nie bedzie tego sprawdzal
        // D | wiec to pominalem; Jak bedziesz chcial - pisz

        // D | Tutaj handlujemy to, gdy jednak mamy juz plik dest
        // D | Jezeli plik src nie jest katalogiem i plik src jest nowszy niz plik dest
        if (!S_ISDIR(srcStat.st_mode) && srcStat.st_mtim.tv_sec > destStat.st_mtim.tv_sec) {
            syslog(LOG_NOTICE, "overwrite %s with %s\n", pathJoin(dest, srcFiles[i]), pathJoin(src, srcFiles[i]));
            if (copy(pathJoin(src, srcFiles[i]), pathJoin(dest, srcFiles[i]), srcStat) != 0) {
                syslog(LOG_ERR, "overwrite %s with %s failed\n", pathJoin(dest, srcFiles[i]), pathJoin(src, srcFiles[i]));
            }
        }
    }

    // D | Konczymy iteracje po plikach w src
    // D | Wyliczamy ile jest plikow w dest i tworzymy katalogow plikow do wywalenia z dest
    int destFilesCount = 0;
    for (; destFiles[destFilesCount] != NULL; ++destFilesCount);
    char** dirsToRemove = malloc(sizeof(char*) * destFilesCount);
    int dirsToRemoveCount = 0;

    for (int i = 0; destFiles[i] != NULL; ++i) {
        dirsToRemove[i] = NULL;

        struct stat srcStat;
        if (stat(pathJoin(src, destFiles[i]), &srcStat) != 0) {

            // D | Jezeli nie ma takiego pliku src jak plik dest
            if (errno == ENOENT) {

                // D | Jezeli plik jest katalogiem, to dodajemy go do listy katalogow do wywalenia
                if (isDir(pathJoin(dest, destFiles[i]))) {
                    dirsToRemove[dirsToRemoveCount++] = pathJoin(dest, destFiles[i]);
                    continue;
                }

                // D | Jezeli plik jest plikiem to usuwamy plik
                syslog(LOG_NOTICE, "remove file %s\n", pathJoin(dest, destFiles[i]));
                if (remove(pathJoin(dest, destFiles[i])) != 0) {
                    syslog(LOG_ERR, "remove file %s failed\n", pathJoin(dest, destFiles[i]));
                }

                continue;
            }

            syslog(LOG_ERR, "Cannot stat source file");
            exit(EXIT_FAILURE);
        }
    }

    // D | Iterujemy po liscie katalogow do wywalenia i je usuwamy
    // D | Jest to specjalnie wyrzucone na koniec, poniewaz jak wczytujemy rekurencyjnie katalogi
    // D | to mamy wczytane w kolejnosci:
    // D |
    // D | - dest/a.txt
    // D | - dest/some_dir
    // D | - dest/some_dir/b.txt
    // D |
    // D | Wiec jakbysmy zaczeli usuwac, to usuwanie `dest/some_dir` by krzyczalo, ze nie moze usunac
    // D | Bo sa pliki w tym katalogu. Wiec wpierw usuwamy pliki, potem katalogi.
    // D |
    // D | Iterujemy od konca, bo jezeli mamy taki przyklad to tez sie przyczepi.
    // D |
    // D | - dest/some_dir
    // D | - dest/some_dir/other_dir
    // D |
    // O | Wlasnie pomyslalem, ze po prostu moznaby iterowac od konca w wyzszym forze i nie trzebaby bylo wyrzucac
    // O | katalogow do oddzielnego fora, ale juz tam czort.
    for (int i = dirsToRemoveCount - 1; i >= 0; --i) {
        syslog(LOG_NOTICE, "remove directory %s\n", dirsToRemove[i]);
        if (rmdir(dirsToRemove[i]) != 0) {
            syslog(LOG_ERR, "remove directory %s failed\n", dirsToRemove[i]);
        }

    }
}

int isDir(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) return 0;

    return S_ISDIR(statbuf.st_mode);
}


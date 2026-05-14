/*   THE HOUSE OF LEAVES: CHANGING-WORD CROSSWORD PUZZLE
 *
 * - Process Hierarchy: the parent process manages the game and spawns
 *   child processes via fork() to display instructions and redraw the board.
 *
 * - Waitpid: the parent process blocks until each child process terminates.
 *
 * - Threads: two POSIX threads run concurrently within the parent process;
 *   one manages word changes over time and the other handles user input.
 *
 * - Signals: SIGUSR1 notifies the parent process to trigger a board redraw,
 *   while SIGINT allows the user to exit the game gracefully via CTRL+C.
 *
 * - Alarms: alarm() schedules a SIGALRM delivery every 60 seconds to display
 *   a motivational message to the player.
 *
 * - Synchronization: a mutex guards all shared variables accessed by both threads.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/wait.h>
#include <ctype.h>
#include <time.h>

/* ══════════════════════════════════════════
   CONSTANTS
   ══════════════════════════════════════════ */

#define NUM_ROWS 17
#define NUM_COLUMNS 10

#define NUM_WORDS 5  

#define MAX_LEN_WORD 10
#define MAX_LEN_DESC 100

#define CHANGE_INTERVAL 20 
#define ALARM_INTERVAL 60   

/* ══════════════════════════════════════════
   WORD STRUCT 
   ══════════════════════════════════════════ */

typedef struct {
    char word[MAX_LEN_WORD];
    char description[MAX_LEN_DESC];
} Word;

Word horizontal1[NUM_WORDS] = {
    {"casa", "Lugar donde viven las personas."},
    {"cara", "Parte del cuerpo donde están los ojos, nariz y boca."},
    {"capa", "Prenda que se usa sobre la ropa, como la de los superhéroes."},
    {"cama", "Mueble donde duermes y descansas."},
    {"caza", "Actividad de buscar y atrapar animales."}
};

Word horizontal2[NUM_WORDS] = {
    {"perro", "Animal doméstico que es conocido como el mejor amigo del hombre."},
    {"pacto", "Acuerdo entre dos o más personas que se comprometen a cumplir lo estipulado."},
    {"pasto", "Hierba que come el ganado en el terreno donde se cría."},
    {"plato", "Utensilio donde se sirve la comida."},
    {"plomo", "Metal pesado de color gris."}
};

Word horizontal3[NUM_WORDS] = {
    {"barro", "Mezcla de tierra y agua que se vuelve blanda."},
    {"carro", "Vehículo con cuatro ruedas que sirve para transportarse."},
    {"barco", "Medio de transporte que navega por el agua."},
    {"jarro", "Recipiente usado para servir líquidos."},
    {"gallo", "Ave doméstica que canta al amanecer."}
};

Word vertical1[NUM_WORDS] = {
    {"ganado", "Conjunto de animales criados en el campo, como vacas u ovejas."},
    {"pasado", "Tiempo que ya ocurrió antes del presente."},
    {"amparo", "Recurso legal que protege los derechos fundamentales frente a abusos de autoridad."},
    {"zapato", "Calzado que se usa para cubrir el pie."},
    {"casado", "Persona que tiene matrimonio."}
};

Word vertical2[NUM_WORDS] = {
    {"planta", "Ser vivo que crece en la tierra y no se mueve, como un árbol o una flor."},
    {"piedra", "Sustancia mineral, más o menos dura y compacta."},
    {"puerta", "Parte de una casa que sirve para entrar o salir."},
    {"prueba", "Evaluación para medir conocimientos o habilidades."},
    {"paloma", "Ave común de color blanco o gris, simboliza la paz."}
};

Word vertical3[NUM_WORDS] = {
    {"comino", "Especia aromática obtenida de las semillas de la planta Cuminum cyminum."},
    {"conejo", "Animal pequeño con orejas largas que salta."},
    {"tomate", "Fruta roja muy usado en la cocina."},
    {"sonido", "Vibración que se puede escuchar con los oídos."},
    {"modelo", "Representación o ejemplo que sirve como referencia."}
};

/* ══════════════════════════════════════════
   SHARED RESOURCES
   ══════════════════════════════════════════ */

char dashboard[NUM_ROWS][NUM_COLUMNS];

char word_h1[5];   // 4 letters + '\0' 
char word_h2[6];   // 5 letters + '\0' 
char word_h3[6];   // 5 letters + '\0' 
char word_v1[7];   // 6 letters + '\0' 
char word_v2[7];   // 6 letters + '\0' 
char word_v3[7];   // 6 letters + '\0' 

int idx_h1, idx_h2, idx_h3; 
int idx_v1, idx_v2, idx_v3;

int solved_h1, solved_h2, solved_h3;
int solved_v1, solved_v2, solved_v3;

int total_solved = 0;
int game_over    = 0;
char feedback[128];

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pid_t parent_pid;

/* ══════════════════════════════════════════
   POSITION HELPERS
   ══════════════════════════════════════════ */

// Return 1 if the cell (row, column) contains a letter from the corresponding word 
// Horizontals
static int in_h1(int row, int column) { 
    return row == 4 && column >= 5 && column <= 8;  
}
static int in_h2(int row, int column) { 
    return row == 6 && column >= 2 && column <= 6;  
}
static int in_h3(int row, int column) { 
    return row == 11 && column >= 1 && column <= 5;  
}
// Verticals
static int in_v1(int row, int column) { 
    return column == 6 && row >= 1 && row <= 6;  
}
static int in_v2(int row, int column) { 
    return column == 2 && row >= 6 && row <= 11; 
}
static int in_v3(int row, int column) { 
    return column == 5 && row >= 10 && row <= 15; 
}

// Returns 1 if the cell is the intersection of two words
static int is_intersection(int row, int column) {
    int count = in_h1(row, column) + in_h2(row, column) + in_h3(row, column)
                + in_v1(row, column) + in_v2(row, column) + in_v3(row, column);
    return count >= 2;
}

/* ══════════════════════════════════════════
    PLACE WORDS ON THE INTERNAL BOARD
    Always keep the actual letters, the
    hiding logic is in print_board
   ══════════════════════════════════════════ */

static void place_words_on_dashboard() {
    for (int i = 0; i < NUM_ROWS; i++)
        for (int j = 0; j < NUM_COLUMNS; j++)
            dashboard[i][j] = ' ';

    // Horizontals
    for (int j = 0; j < 4; j++)
        dashboard[4][5 + j] = word_h1[j];

    for (int j = 0; j < 5; j++)
        dashboard[6][2 + j] = word_h2[j];

    for (int j = 0; j < 5; j++)
        dashboard[11][1 + j] = word_h3[j];

    // Verticals
    for (int i = 0; i < 6; i++)
        dashboard[1 + i][6] = word_v1[i];

    for (int i = 0; i < 6; i++)
        dashboard[6 + i][2] = word_v2[i];

    for (int i = 0; i < 6; i++)
        dashboard[10 + i][5] = word_v3[i];
}

/* ══════════════════════════════════════════
   FUNCTION RESPONSIBLE FOR INITIALIZING THE GAME
   ══════════════════════════════════════════ */

static void init_game() {
    srand((unsigned)time(NULL));

    idx_h1 = rand() % NUM_WORDS;
    idx_h2 = rand() % NUM_WORDS;
    idx_h3 = rand() % NUM_WORDS;
    idx_v1 = rand() % NUM_WORDS;
    idx_v2 = rand() % NUM_WORDS;
    idx_v3 = rand() % NUM_WORDS;

    strcpy(word_h1, horizontal1[idx_h1].word);
    strcpy(word_h2, horizontal2[idx_h2].word);
    strcpy(word_h3, horizontal3[idx_h3].word);
    strcpy(word_v1, vertical1[idx_v1].word);
    strcpy(word_v2, vertical2[idx_v2].word);
    strcpy(word_v3, vertical3[idx_v3].word);

    solved_h1 = solved_h2 = solved_h3 = 0;
    solved_v1 = solved_v2 = solved_v3 = 0;
    total_solved = 0;
    game_over = 0;
    strcpy(feedback, "El juego inicia. Descubre los secretos de la casa.");

    place_words_on_dashboard();
}

/* ══════════════════════════════════════════
   PRINT BOARD
   (executed exclusively in the child fork)

   Display rules per cell:
    * Empty (‘ ’) -> gray dot
    * Unrevealed intersection -> yellow ‘*’
    * Solved word cell -> green letter
    * Unsolved word cell:
        displays the clue number in gray
   ══════════════════════════════════════════ */

static void print_board() {
    printf("\033[2J\033[H");

    printf("\033[33;1m"
           "\n  ╔══════════════════════════════╗\n"
           "  ║       LA CASA DE HOJAS       ║\n"
           "  ╚══════════════════════════════╝\n"
           "\033[0m\n");

    for (int i = 0; i < NUM_ROWS; i++) {
        printf("  ");
        for (int j = 0; j < NUM_COLUMNS; j++) {
            char ch = dashboard[i][j];

            // Empty cell
            if (ch == ' ') {
                printf("\033[90m . \033[0m");
                continue;
            }

            // Determine which words this cell belongs to 
            int belongs_h1 = in_h1(i,j), belongs_h2 = in_h2(i,j), belongs_h3 = in_h3(i,j);
            int belongs_v1 = in_v1(i,j), belongs_v2 = in_v2(i,j), belongs_v3 = in_v3(i,j);

            int revealed = (belongs_h1 && solved_h1) || (belongs_h2 && solved_h2)
                        || (belongs_h3 && solved_h3) || (belongs_v1 && solved_v1)
                        || (belongs_v2 && solved_v2) || (belongs_v3 && solved_v3);

            if (revealed) {
                // Display actual text in green 
                printf("\033[32;1m %c \033[0m", (char)toupper((unsigned char)ch));
                continue;
            }

            // Unrevealed cell that is an intersection 
            if (is_intersection(i,j)) {
                printf("\033[33;1m * \033[0m");
                continue;
            }

            // Unresolved single-word cell, display clue number 
            int clue = 0;
            if (belongs_h1) 
                clue = 1;
            else if (belongs_h2) 
                clue = 2;
            else if (belongs_h3) 
                clue = 3;
            else if (belongs_v1) 
                clue = 4;
            else if (belongs_v2) 
                clue = 5;
            else if (belongs_v3) 
                clue = 6;

            printf("\033[90m %d \033[0m", clue);
        }
        printf("\n");
    }

    printf("\n\033[36;1m  ── HORIZONTALES ─────────────────────────────────────────\033[0m\n");
    printf(solved_h1 ? "  \033[32m1. RESUELTA\033[0m\n"
                     : "  \033[37m1. %s\033[0m\n", horizontal1[idx_h1].description);
    printf(solved_h2 ? "  \033[32m2. RESUELTA\033[0m\n"
                     : "  \033[37m2. %s\033[0m\n", horizontal2[idx_h2].description);
    printf(solved_h3 ? "  \033[32m3. RESUELTA\033[0m\n"
                     : "  \033[37m3. %s\033[0m\n", horizontal3[idx_h3].description);

    printf("\033[34;1m  ── VERTICALES ───────────────────────────────────────────\033[0m\n");
    printf(solved_v1 ? "  \033[32m4. RESUELTA\033[0m\n"
                     : "  \033[37m4. %s\033[0m\n", vertical1[idx_v1].description);
    printf(solved_v2 ? "  \033[32m5. RESUELTA\033[0m\n"
                     : "  \033[37m5. %s\033[0m\n", vertical2[idx_v2].description);
    printf(solved_v3 ? "  \033[32m6. RESUELTA\033[0m\n"
                     : "  \033[37m6. %s\033[0m\n", vertical3[idx_v3].description);

    printf("\n  \033[35;1m-> %s\033[0m\n", feedback);
    printf("  \033[90mResueltas: %d/6\033[0m\n", total_solved);

    if (!game_over)
        printf("  \033[33m\n  Número de pregunta (CTRL + C = salir): \033[0m");
}

/* ══════════════════════════════════════════
   RENDER: parent process forks, 
           child process prints,
           parent process waits with waitpid
   ══════════════════════════════════════════ */

static volatile int render_flag = 0;

static void handler_sigusr1(int sig) {
    render_flag = 1;
}

/* ══════════════════════════════════════════
   FORK — A child is created every time the board needs to be printed
   ══════════════════════════════════════════ */
static void do_render() {
    render_flag = 0;
    pid_t child = fork();

    if (child < 0) { 
        perror("fork"); 
        return; 
    }
    if (child == 0) {
        print_board();
        exit(0);
    }
    waitpid(child, NULL, 0);
}

/* ══════════════════════════════════════════
   THREAD 1 — CHANGE MANAGER
   ══════════════════════════════════════════ */
static void *thread_changes(void *arg) {
    while (1) {
        sleep(CHANGE_INTERVAL); 

        pthread_mutex_lock(&mutex);

        if (game_over) { 
            pthread_mutex_unlock(&mutex); 
            break; 
        }

        int slots[6];   
        int n = 0;
        
        if (!solved_h1) 
            slots[n++] = 1;
        if (!solved_h2) 
            slots[n++] = 2;
        if (!solved_h3) 
            slots[n++] = 3;
        if (!solved_v1) 
            slots[n++] = 4;
        if (!solved_v2) 
            slots[n++] = 5;
        if (!solved_v3) 
            slots[n++] = 6;

        if (n > 0) {
            int slot = slots[rand() % n];
            int next;
            switch (slot) {
                case 1:
                    next = (idx_h1 + 1) % NUM_WORDS;  
                    idx_h1 = next;
                    strcpy(word_h1, horizontal1[next].word);
                    snprintf(feedback, 127, "La casa muta. Pista 1 ha cambiado.");
                    break;
                case 2:
                    next = (idx_h2 + 1) % NUM_WORDS;  
                    idx_h2 = next;
                    strcpy(word_h2, horizontal2[next].word);
                    snprintf(feedback, 127, "La casa muta. Pista 2 ha cambiado.");
                    break;
                case 3:
                    next = (idx_h3 + 1) % NUM_WORDS;  
                    idx_h3 = next;
                    strcpy(word_h3, horizontal3[next].word);
                    snprintf(feedback, 127, "La casa muta. Pista 3 ha cambiado.");
                    break;
                case 4:
                    next = (idx_v1 + 1) % NUM_WORDS;  
                    idx_v1 = next;
                    strcpy(word_v1, vertical1[next].word);
                    snprintf(feedback, 127, " La casa muta. Pista 4 ha cambiado.");
                    break;
                case 5:
                    next = (idx_v2 + 1) % NUM_WORDS;  
                    idx_v2 = next;
                    strcpy(word_v2, vertical2[next].word);
                    snprintf(feedback, 127, "La casa muta. Pista 5 ha cambiado.");
                    break;
                case 6:
                    next = (idx_v3 + 1) % NUM_WORDS;  
                    idx_v3 = next;
                    strcpy(word_v3, vertical3[next].word);
                    snprintf(feedback, 127, " La casa muta. Pista 6 ha cambiado.");
                    break;
            }
            place_words_on_dashboard();
        }

        pthread_mutex_unlock(&mutex);

        kill(parent_pid, SIGUSR1);   // Refresh terminal content
    }
    return NULL;
}

/* ══════════════════════════════════════════
   THREAD 2 — INPUT MANAGER
   ══════════════════════════════════════════ */

static void str_upper(char *s) {
    for ( ; *s; s++) 
        *s = (char)toupper((unsigned char)*s);
}

static void *thread_input(void *arg) {
    char num_buf[8];
    char ans_buf[MAX_LEN_WORD + 4];

    while (1) {
        if (!fgets(num_buf, sizeof(num_buf), stdin)) 
            break;
        int n = atoi(num_buf);

        if (n < 1 || n > 6) {
            pthread_mutex_lock(&mutex);
            snprintf(feedback, 127, "Numero inválido. Elige entre 1 y 6.");
            pthread_mutex_unlock(&mutex);
            kill(parent_pid, SIGUSR1);
            continue;
        }

        int already = (n==1 && solved_h1) || (n==2 && solved_h2) || (n==3 && solved_h3)
                   || (n==4 && solved_v1) || (n==5 && solved_v2) || (n==6 && solved_v3);

        // Already answered
        if (already) {
            pthread_mutex_lock(&mutex);
            snprintf(feedback, 127, "La palabra %d ya fue resuelta.", n);
            pthread_mutex_unlock(&mutex);
            kill(parent_pid, SIGUSR1);
            continue;
        }

        printf("\033[33m  Respuesta: \033[0m");

        if (!fgets(ans_buf, sizeof(ans_buf), stdin)) 
            break;
        ans_buf[strcspn(ans_buf, "\n\r")] = '\0';
        str_upper(ans_buf);

        pthread_mutex_lock(&mutex);
        if (game_over) { 
            pthread_mutex_unlock(&mutex); 
            break; 
        }


        // Current correct answer
        char correct[MAX_LEN_WORD];
        switch (n) {
            case 1: 
                strcpy(correct, word_h1); 
                break;
            case 2: 
                strcpy(correct, word_h2); 
                break;
            case 3: 
                strcpy(correct, word_h3); 
                break;
            case 4: 
                strcpy(correct, word_v1); 
                break;
            case 5: 
                strcpy(correct, word_v2); 
                break;
            case 6: 
                strcpy(correct, word_v3); 
                break;
            default: 
                correct[0] = '\0';
        }
        str_upper(correct);

        if (strcmp(ans_buf, correct) == 0) {
            switch (n) {
                case 1: 
                    solved_h1 = 1; 
                    break;
                case 2: 
                    solved_h2 = 1; 
                    break;
                case 3: 
                    solved_h3 = 1; 
                    break;
                case 4: 
                    solved_v1 = 1; 
                    break;
                case 5: 
                    solved_v2 = 1; 
                    break;
                case 6: 
                    solved_v3 = 1; 
                    break;
            }
            total_solved++;

            if (total_solved == 6) {
                game_over = 1;
                strcpy(feedback, "¡¡FELICIDADES!! Has descifrado todos los secretos de la casa.");
            } else {
                snprintf(feedback, 127, "¡Correcto! Pista %d resuelta.", n);
            }
        } else {
            snprintf(feedback, 127, "Incorrecto para pista %d. Sigue intentando.", n);
        }

        pthread_mutex_unlock(&mutex);
        kill(parent_pid, SIGUSR1);

        if (game_over) 
            break;
    }
    return NULL;
}

void handler_sigint(int s){
    printf("\033[33m\n  ¡Hasta pronto! Esperamos verte de nuevo en La Casa de Hojas.\033[0m\n");
    game_over = 1;
    exit(1);
}

static void handler_sigalrm(int sig) {
    static int alarm_count = 0;
    static const char *phrases[] = {
        "¿Tan difícil es? La casa lleva esperando mucho tiempo...",
        "El crucigrama no se va a resolver solo, ¿o sí?",
        "Otros jugadores ya van por la mitad. ¿Y tú?",
        "La casa tiene paciencia. ¿La tienes tú?",
        "Tick tock... la casa sigue cambiando."
    };

    alarm_count++;
    int phrase_idx = (alarm_count - 1) % (sizeof(phrases) / sizeof(phrases[0]));
    printf("\033[33m\n  [!] Llevas %d minuto(s) jugando. %s\033[0m\n", alarm_count, phrases[phrase_idx]);
    alarm(ALARM_INTERVAL);
}

/* ══════════════════════════════════════════
   MAIN
   ══════════════════════════════════════════ */

int main() {
    parent_pid = getpid();

    // SIGUSR1: fork+render
    signal(SIGUSR1, handler_sigusr1);
    // SIGINT: finish game
    signal(SIGINT, handler_sigint);

    pid_t instr_pid = fork();
    if(instr_pid == 0){
        printf("\n\033[33;1mInstrucciones del juego:\033[0m\n");
        printf("• Escribe el número de la palabra (1 - 6) que deseas adivinar.\n");
        printf("• Después, escribe la palabra en minúsculas o mayúsculas (no utilices acentos).\n");
        printf("• Si aciertas, la palabra se revela en el tablero.\n");
        printf("• Si fallas, la palabra puede cambiar con el tiempo.\n");
        printf("• Una palabra cambiará cada %d segundos aleatoriamente.\n", CHANGE_INTERVAL);
        printf("• Oprime CRTL + C en cualquier momento para salir.\n\n");
        printf("\033[33;1m⚠  IMPORTANTE:\033[0m\033[37m Si el crucigrama cambia mientras escribes, termina tu respuesta normal.\n");
        printf("   Si la pista cambió, responde a la nueva.\033[0m\n\n");
        printf("Presiona ENTER para continuar...\n");
        while (getchar() != '\n'); 
        exit(0);
    }
    waitpid(instr_pid, NULL, 0);

    init_game();
    do_render();  

    pthread_t tid_cambios, tid_input;
    pthread_create(&tid_cambios, NULL, thread_changes, NULL);
    pthread_create(&tid_input,   NULL, thread_input,   NULL);

    signal(SIGALRM, handler_sigalrm);
    alarm(ALARM_INTERVAL);

    while (1) {
        pause();
        do_render();

        pthread_mutex_lock(&mutex);
        int over = game_over;
        pthread_mutex_unlock(&mutex);
        if (over) 
            break;
    }

    pthread_cancel(tid_cambios);
    pthread_cancel(tid_input);
    pthread_join(tid_cambios, NULL);
    pthread_join(tid_input,   NULL);

    printf("\n\033[36m  Gracias por jugar La Casa de Hojas.\033[0m\n\n");
    return 0;
}
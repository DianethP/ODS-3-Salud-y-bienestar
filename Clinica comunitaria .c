#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#define MAX_COLA 100
#define MAX_PILA 50
#define MAX_HISTORIAL 200

// ---------- ESTRUCTURAS ----------
struct Paciente {
    int folio;
    char nombre[50];
    int edad;
    char motivo[30];
    int prioridad;      // 0 = normal, 1 = prioritaria
    time_t llegada;     // marca de tiempo al registrarse
    time_t atencion;    // marca de tiempo al ser atendido
};

struct Cola {
    struct Paciente datos[MAX_COLA];
    int frente;     // índice circular
    int final;      // índice circular
    int cantidad;   // elementos en cola
};

struct Historial {
    struct Paciente datos[MAX_HISTORIAL];
    int total;
};

// --- Acciones para UNDO ---
enum TipoAccion { ACC_REGISTRAR, ACC_ATENDER };

struct Accion {
    enum TipoAccion tipo;
    struct Paciente p_snapshot;
};

struct PilaAcciones {
    struct Accion datos[MAX_PILA];
    int tope; // -1 vacía
};

// ---------- PROTOTIPOS ----------
// cola
void inicializarCola(struct Cola *c);
int colaVacia(struct Cola c);
int colaLlena(struct Cola c);
void encolar(struct Cola *c, struct Paciente p);
struct Paciente desencolar(struct Cola *c);
void encolarFrente(struct Cola *c, struct Paciente p);

// util cola
int offsetPorFolio(const struct Cola *c, int folio);
struct Paciente quitarPorOffset(struct Cola *c, int offset);

// pila acciones (undo)
void inicializarPila(struct PilaAcciones *p);
int pilaVacia(struct PilaAcciones p);
int pushAccion(struct PilaAcciones *p, struct Accion a);
int popAccion(struct PilaAcciones *p, struct Accion *out);

// lógica
void registrarPaciente(struct Cola *c, int *folio, struct PilaAcciones *pila);
void atenderPaciente(struct Cola *c, struct Historial *h, struct PilaAcciones *pila);
void mostrarHistorial(struct Historial h);
void mostrarCola(const struct Cola *cola);
void buscarPaciente(struct Cola c, struct Historial h);
void generarReporte(struct Cola c, struct Historial h);
void deshacerUltimaAccion(struct Cola *c, struct Historial *h, struct PilaAcciones *pila);

// helpers de reporte
int contiene_ins(const char *txt, const char *sub);
int categoriaMotivo(const char *motivo); // 0 tos, 1 fiebre, 2 curacion, 3 control, 4 otros
int franjaHoraria(int hour); // 0 madrugada, 1 mañana, 2 tarde, 3 noche

// ---------- FUNCION PRINCIPAL ----------
int main() {
    struct Cola cola;
    struct Historial historial;
    struct PilaAcciones pila;
    int folio = 1;
    int opcion;

    inicializarCola(&cola);
    historial.total = 0;
    inicializarPila(&pila);

    do {
        printf("\n===== MENU CLINICA COMUNITARIA =====\n");
        printf("1. Registrar paciente\n");
        printf("2. Atender paciente (prioriza casos 1)\n");
        printf("3. Mostrar historial\n");
        printf("4. Mostrar pacientes en espera\n");
        printf("5. Buscar paciente por folio\n");
        printf("6. Generar reporte\n");
        printf("7. DESHACER ultima accion\n");
        printf("8. Salir\n");
        printf("Seleccione una opcion: ");
        if (scanf("%d", &opcion) != 1) { opcion = 8; }
        getchar(); 

        switch (opcion) {
            case 1: registrarPaciente(&cola, &folio, &pila); break;
            case 2: atenderPaciente(&cola, &historial, &pila); break;
            case 3: mostrarHistorial(historial); break;
            case 4: mostrarCola(&cola); break;
            case 5: buscarPaciente(cola, historial); break;
            case 6: generarReporte(cola, historial); break;
            case 7: deshacerUltimaAccion(&cola, &historial, &pila); break;
            case 8: printf("Saliendo del sistema...\n"); break;
            default: printf("Opcion invalida. Intente de nuevo.\n");
        }

    } while (opcion != 8);

    return 0;
}

// ---------- FUNCIONES DE COLA ----------
void inicializarCola(struct Cola *c) {
    c->frente = 0;
    c->final = -1;
    c->cantidad = 0;
}

int colaVacia(struct Cola c) { return c.cantidad == 0; }
int colaLlena(struct Cola c) { return c.cantidad == MAX_COLA; }

void encolar(struct Cola *c, struct Paciente p) {
    if (colaLlena(*c)) {
        printf("Cola llena. No se puede registrar al paciente.\n");
        return;
    }
    c->final = (c->final + 1) % MAX_COLA;
    c->datos[c->final] = p;
    c->cantidad++;
}

struct Paciente desencolar(struct Cola *c) {
    struct Paciente p = c->datos[c->frente];
    c->frente = (c->frente + 1) % MAX_COLA;
    c->cantidad--;
    return p;
}

void encolarFrente(struct Cola *c, struct Paciente p) {
    if (colaLlena(*c)) {
        printf("Cola llena. No se puede reinsertar al frente.\n");
        return;
    }
    c->frente = (c->frente - 1 + MAX_COLA) % MAX_COLA;
    c->datos[c->frente] = p;
    c->cantidad++;
}

// --- util cola ---
int offsetPorFolio(const struct Cola *c, int folio) {
    for (int k = 0; k < c->cantidad; k++) {
        int idx = (c->frente + k) % MAX_COLA;
        if (c->datos[idx].folio == folio) return k; // offset desde el frente
    }
    return -1;
}

// Quita elemento en offset (0..cantidad-1) manteniendo orden; retorna el paciente removido
struct Paciente quitarPorOffset(struct Cola *c, int offset) {
    // rotar 'offset' elementos al final
    for (int i = 0; i < offset; i++) {
        struct Paciente x = desencolar(c);
        encolar(c, x);
    }
    // ahora el que queremos está al frente
    return desencolar(c);
}

// ---------- PILA ACCIONES (UNDO) ----------
void inicializarPila(struct PilaAcciones *p) { p->tope = -1; }
int pilaVacia(struct PilaAcciones p) { return p.tope < 0; }

int pushAccion(struct PilaAcciones *p, struct Accion a) {
    if (p->tope + 1 >= MAX_PILA) {
        printf("Pila de acciones llena. No se registrara UNDO para esta accion.\n");
        return 0;
    }
    p->datos[++(p->tope)] = a;
    return 1;
}

int popAccion(struct PilaAcciones *p, struct Accion *out) {
    if (pilaVacia(*p)) return 0;
    *out = p->datos[(p->tope)--];
    return 1;
}

// ---------- LOGICA ----------
void registrarPaciente(struct Cola *c, int *folio, struct PilaAcciones *pila) {
    struct Paciente p;

    if (colaLlena(*c)) {
        printf("La cola esta llena. No se puede agregar mas pacientes.\n");
        return;
    }

    p.folio = *folio;
    printf("\n--- Registro de paciente ---\n");
    printf("Nombre: ");
    fgets(p.nombre, sizeof(p.nombre), stdin);
    p.nombre[strcspn(p.nombre, "\n")] = 0;

    printf("Edad: ");
    scanf("%d", &p.edad);
    getchar();

    printf("Motivo (tos/fiebre/curacion/control/otro): ");
    fgets(p.motivo, sizeof(p.motivo), stdin);
    p.motivo[strcspn(p.motivo, "\n")] = 0;

    printf("Prioridad (0 = Normal, 1 = Alta): ");
    scanf("%d", &p.prioridad);
    getchar();

    p.llegada = time(NULL);
    p.atencion = 0;

    encolar(c, p);
    pushAccion(pila, (struct Accion){ ACC_REGISTRAR, p });

    printf("Paciente registrado con folio #%d\n", p.folio);
    (*folio)++;
}

void atenderPaciente(struct Cola *c, struct Historial *h, struct PilaAcciones *pila) {
    if (colaVacia(*c)) {
        printf("No hay pacientes en espera.\n");
        return;
    }

    int posPrioritario = -1;
    for (int k = 0; k < c->cantidad; k++) {
        int idx = (c->frente + k) % MAX_COLA;
        if (c->datos[idx].prioridad == 1) { posPrioritario = k; break; }
    }

    // offset a quitar: 0 si no hay prioritarios
    int offset = (posPrioritario != -1 ? posPrioritario : 0);
    struct Paciente p = quitarPorOffset(c, offset);

    p.atencion = time(NULL);

    // Agregar al historial
    if (h->total >= MAX_HISTORIAL) {
        printf("Historial lleno. No se puede registrar la atencion.\n");
        return;
    }
    h->datos[h->total++] = p;

    pushAccion(pila, (struct Accion){ ACC_ATENDER, p });

    printf("\n--- Paciente atendido ---\n");
    printf("Folio: %d | Nombre: %s | Motivo: %s | Prioridad: %s\n",
           p.folio, p.nombre, p.motivo, p.prioridad == 1 ? "Alta (Prioritaria)" : "Normal");
}

void mostrarHistorial(struct Historial h) {
    if (h.total == 0) {
        printf("No hay pacientes atendidos.\n");
        return;
    }

    printf("\n--- HISTORIAL DE PACIENTES ---\n");
    for (int i = 0; i < h.total; i++) {
        double esperaMin = 0.0;
        if (h.datos[i].atencion && h.datos[i].llegada)
            esperaMin = difftime(h.datos[i].atencion, h.datos[i].llegada) / 60.0;

        printf("Folio: %d | Nombre: %s | Edad: %d | Motivo: %s | Prioridad: %s | Espera aprox: %.1f min\n",
               h.datos[i].folio, h.datos[i].nombre, h.datos[i].edad, h.datos[i].motivo,
               h.datos[i].prioridad == 1 ? "Alta" : "Normal", esperaMin);
    }
}

void mostrarCola(const struct Cola *cola) {
    if (colaVacia(*cola)) {
        printf("No hay pacientes en espera.\n");
        return;
    }

    printf("\n--- PACIENTES EN ESPERA ---\n");
    for (int k = 0; k < cola->cantidad; k++) {
        int idx = (cola->frente + k) % MAX_COLA;
        struct Paciente p = cola->datos[idx];
        printf("Folio: %d | Nombre: %s | Edad: %d | Motivo: %s | Prioridad: %s\n",
               p.folio, p.nombre, p.edad, p.motivo, p.prioridad == 1 ? "Alta" : "Normal");
    }
}

void buscarPaciente(struct Cola c, struct Historial h) {
    int folioBuscado;
    int encontrado = 0;

    printf("Ingrese el folio del paciente a buscar: ");
    scanf("%d", &folioBuscado);
    getchar();

    // Buscar en cola (espera)
    for (int k = 0; k < c.cantidad; k++) {
        int idx = (c.frente + k) % MAX_COLA;
        if (c.datos[idx].folio == folioBuscado) {
            printf("\nPaciente encontrado en espera:\n");
            printf("Nombre: %s | Motivo: %s | Prioridad: %s\n",
                   c.datos[idx].nombre, c.datos[idx].motivo,
                   c.datos[idx].prioridad == 1 ? "Alta" : "Normal");
            encontrado = 1;
            break;
        }
    }

    // Buscar en historial (ya atendidos)
    if (!encontrado) {
        for (int i = 0; i < h.total; i++) {
            if (h.datos[i].folio == folioBuscado) {
                printf("\nPaciente encontrado en historial:\n");
                printf("Nombre: %s | Motivo: %s | Prioridad: %s\n",
                       h.datos[i].nombre, h.datos[i].motivo,
                       h.datos[i].prioridad == 1 ? "Alta" : "Normal");
                encontrado = 1;
                break;
            }
        }
    }

    if (!encontrado)
        printf("No se encontro ningun paciente con ese folio.\n");
}

void generarReporte(struct Cola c, struct Historial h) {
    printf("\n=========== REPORTE GENERAL (hoy) ===========\n");
    printf("Pacientes en espera: %d\n", c.cantidad);
    printf("Pacientes atendidos : %d\n", h.total);

    int prioritarios = 0, normales = 0;
    double sumaEsperaMin = 0.0;

    // motivos: 0 tos, 1 fiebre, 2 curacion, 3 control, 4 otros
    int causas[5] = {0};
    const char *lblCausas[5] = { "Tos", "Fiebre", "Curacion", "Control", "Otros" };

    // franjas: 0 madrugada(22-5), 1 mañana(6-11), 2 tarde(12-17), 3 noche(18-21)
    int franjas[4] = {0};
    const char *lblFranjas[4] = { "Madrugada", "Manana", "Tarde", "Noche" };

    for (int i = 0; i < h.total; i++) {
        struct Paciente *p = &h.datos[i];
        if (p->prioridad == 1) prioritarios++; else normales++;

        if (p->atencion && p->llegada) {
            sumaEsperaMin += difftime(p->atencion, p->llegada) / 60.0;
            int hour = localtime(&p->atencion)->tm_hour;
            franjas[franjaHoraria(hour)]++;
        }

        causas[categoriaMotivo(p->motivo)]++;
    }

    double promEspera = (h.total > 0) ? (sumaEsperaMin / h.total) : 0.0;

    printf("\n-- Prioridad --\n");
    printf("  Prioritarios atendidos: %d\n", prioritarios);
    printf("  Normales atendidos   : %d\n", normales);

    printf("\n-- Tiempo de espera --\n");
    printf("  Espera promedio aprox: %.1f minutos\n", promEspera);

    printf("\n-- Causas mas frecuentes --\n");
    for (int i = 0; i < 5; i++)
        printf("  %-10s : %d\n", lblCausas[i], causas[i]);

    printf("\n-- Atenciones por franja horaria --\n");
    for (int i = 0; i < 4; i++)
        printf("  %-10s : %d\n", lblFranjas[i], franjas[i]);

    printf("=============================================\n");
}

void deshacerUltimaAccion(struct Cola *c, struct Historial *h, struct PilaAcciones *pila) {
    struct Accion a;
    if (!popAccion(pila, &a)) {
        printf("No hay acciones para deshacer.\n");
        return;
    }

    switch (a.tipo) {
        case ACC_REGISTRAR: {
            int off = offsetPorFolio(c, a.p_snapshot.folio);
            if (off != -1) {
                (void)quitarPorOffset(c, off);
                printf("Deshacer: se elimino el registro del folio %d de la cola.\n", a.p_snapshot.folio);
            } else {
                printf("Deshacer: el folio %d ya no esta en cola (posiblemente atendido).\n", a.p_snapshot.folio);
            }
            break;
        }
        case ACC_ATENDER: {
            if (h->total > 0 && h->datos[h->total - 1].folio == a.p_snapshot.folio) {
                struct Paciente p = h->datos[--(h->total)];
                p.atencion = 0; // vuelve a estar en espera
                encolarFrente(c, p);
                printf("Deshacer: se revirtio la atencion y se reingreso a cola el folio %d.\n", p.folio);
            } else {
                int pos = -1;
                for (int i = h->total - 1; i >= 0; i--) {
                    if (h->datos[i].folio == a.p_snapshot.folio) { pos = i; break; }
                }
                if (pos != -1) {
                    struct Paciente p = h->datos[pos];
                    for (int i = pos; i < h->total - 1; i++) h->datos[i] = h->datos[i + 1];
                    h->total--;
                    p.atencion = 0;
                    encolarFrente(c, p);
                    printf("Deshacer: se revirtio la atencion (busqueda) y se reingreso folio %d.\n", p.folio);
                } else {
                    printf("Deshacer: no se encontro el folio %d en historial.\n", a.p_snapshot.folio);
                }
            }
            break;
        }
        default: break;
    }
}

// ---------- HELPERS DE REPORTE ----------
int contiene_ins(const char *txt, const char *sub) {
    if (!txt || !sub) return 0;
    size_t n = strlen(txt), m = strlen(sub);
    if (m == 0 || m > n) return 0;
    for (size_t i = 0; i + m <= n; i++) {
        size_t k = 0;
        while (k < m && tolower((unsigned char)txt[i + k]) == tolower((unsigned char)sub[k])) k++;
        if (k == m) return 1;
    }
    return 0;
}

int categoriaMotivo(const char *motivo) {
    if (contiene_ins(motivo, "tos")) return 0;
    if (contiene_ins(motivo, "fiebre")) return 1;
    if (contiene_ins(motivo, "curacion") || contiene_ins(motivo, "curación")) return 2;
    if (contiene_ins(motivo, "control")) return 3;
    return 4;
}

int franjaHoraria(int hour) {
    // madrugada 22-5, mañana 6-11, tarde 12-17, noche 18-21
    if (hour >= 22 || hour <= 5) return 0;
    if (hour >= 6 && hour <= 11) return 1;
    if (hour >= 12 && hour <= 17) return 2;
    return 3;
}

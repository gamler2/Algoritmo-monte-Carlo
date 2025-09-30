// montecarlo_fixed.c
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

// ==================== CONFIGURACIÓN ====================
#define MAX_THREADS 16
#define MAX_PROCESSES 16
#define MAP_NAME_MAX 256

// ==================== ESTRUCTURAS DE DATOS ====================
typedef struct {
    long long points_per_thread;
    long long points_inside;
    unsigned int seed;
    int thread_id;
    int method;
} thread_data_t;

typedef struct {
    long long total_points;
    volatile long long points_inside;
    int num_workers;
    int method;
    double needle_length;
    double line_spacing;
    char mutex_name[64]; // Compartimos el nombre del mutex, NO el HANDLE
} shared_data_t;

// ==================== GENERACIÓN DE NÚMEROS ALEATORIOS THREAD-SAFE ====================
unsigned int rand_win(unsigned int* seed) {
    *seed = (*seed * 1103515245u + 12345u) & 0x7FFFFFFFu;
    return *seed;
}

double rand_double_win(unsigned int* seed) {
    return (double)rand_win(seed) / (double)0x7FFFFFFF;
}

// ==================== ALGORITMO DARTBOARD ====================
DWORD WINAPI dartboard_thread_worker(LPVOID lpParam) {
    thread_data_t* data = (thread_data_t*)lpParam;
    data->points_inside = 0;
    
    for (long long i = 0; i < data->points_per_thread; i++) {
        double x = rand_double_win(&data->seed);
        double y = rand_double_win(&data->seed);
        
        if (x * x + y * y <= 1.0) {
            data->points_inside++;
        }
    }
    
    printf("Hilo %d completado: %lld puntos dentro de %lld\n", 
           data->thread_id, data->points_inside, data->points_per_thread);
    
    return 0;
}

// Nota: ahora recibimos HANDLE hMutexLocal para sincronizar localmente
void dartboard_process_worker(int worker_id, shared_data_t* shared, HANDLE hMutexLocal) {
    unsigned int seed = GetCurrentProcessId() ^ GetTickCount() ^ worker_id;
    long long local_inside = 0;
    long long points_per_process = shared->total_points / shared->num_workers;
    
    for (long long i = 0; i < points_per_process; i++) {
        double x = rand_double_win(&seed);
        double y = rand_double_win(&seed);
        
        if (x * x + y * y <= 1.0) {
            local_inside++;
        }
    }
    
    // Sumar al contador compartido de forma sincronizada usando hMutexLocal
    if (hMutexLocal) WaitForSingleObject(hMutexLocal, INFINITE);
    shared->points_inside += local_inside;
    if (hMutexLocal) ReleaseMutex(hMutexLocal);
    
    printf("Proceso %d (PID %lu): %lld puntos dentro\n", 
           worker_id, GetCurrentProcessId(), local_inside);
}

// ==================== ALGORITMO NEEDLES ====================
DWORD WINAPI needles_thread_worker(LPVOID lpParam) {
    thread_data_t* data = (thread_data_t*)lpParam;
    data->points_inside = 0;
    
    for (long long i = 0; i < data->points_per_thread; i++) {
        double center_y = rand_double_win(&data->seed) * 1.0; // line_spacing = 1.0
        double angle = rand_double_win(&data->seed) * 3.14159265358979323846;
        
        double half_length = 0.5; // needle_length = 1.0
        double y_min = center_y - half_length * sin(angle);
        double y_max = center_y + half_length * sin(angle);
        
        if (y_min <= 0.0 || y_max >= 1.0) {
            data->points_inside++;
        }
    }
    
    printf("Hilo %d completado: %lld cruces de %lld\n", 
           data->thread_id, data->points_inside, data->points_per_thread);
    
    return 0;
}

// Recibe hMutexLocal para usarlo en la sincronización
void needles_process_worker(int worker_id, shared_data_t* shared, HANDLE hMutexLocal) {
    unsigned int seed = GetCurrentProcessId() ^ GetTickCount() ^ worker_id;
    long long local_crossings = 0;
    long long needles_per_process = shared->total_points / shared->num_workers;
    
    for (long long i = 0; i < needles_per_process; i++) {
        double center_y = rand_double_win(&seed) * shared->line_spacing;
        double angle = rand_double_win(&seed) * 3.14159265358979323846;
        
        double half_length = shared->needle_length / 2.0;
        double y_min = center_y - half_length * sin(angle);
        double y_max = center_y + half_length * sin(angle);
        
        if (y_min <= 0.0 || y_max >= shared->line_spacing) {
            local_crossings++;
        }
    }
    
    // Sumar al contador compartido de forma sincronizada
    if (hMutexLocal) WaitForSingleObject(hMutexLocal, INFINITE);
    shared->points_inside += local_crossings;
    if (hMutexLocal) ReleaseMutex(hMutexLocal);
    
    printf("Proceso %d (PID %lu): %lld cruces\n", 
           worker_id, GetCurrentProcessId(), local_crossings);
}

// ==================== IMPLEMENTACIÓN CON THREADS (Windows API) ====================
double parallel_threads_monte_carlo(long long total_points, int num_threads, int method) {
    HANDLE threads[MAX_THREADS];
    thread_data_t thread_data[MAX_THREADS];
    long long points_per_thread = total_points / num_threads;
    long long total_inside = 0;
    
    printf("\n=== INICIANDO THREADS (%d hilos, %lld puntos totales) ===\n", 
           num_threads, total_points);
    
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    // Crear threads
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].points_per_thread = points_per_thread;
        thread_data[i].seed = GetTickCount() ^ (i + 1) ^ GetCurrentThreadId();
        thread_data[i].thread_id = i;
        thread_data[i].method = method;
        
        if (method == 1) {
            threads[i] = CreateThread(NULL, 0, dartboard_thread_worker, &thread_data[i], 0, NULL);
        } else {
            threads[i] = CreateThread(NULL, 0, needles_thread_worker, &thread_data[i], 0, NULL);
        }
        
        if (threads[i] == NULL) {
            fprintf(stderr, "Error creando thread %d: %lu\n", i, GetLastError());
            exit(1);
        }
    }
    
    // Esperar a que todos los threads terminen
    WaitForMultipleObjects(num_threads, threads, TRUE, INFINITE);
    
    QueryPerformanceCounter(&end);
    double elapsed = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
    
    // Recolectar resultados
    for (int i = 0; i < num_threads; i++) {
        total_inside += thread_data[i].points_inside;
        CloseHandle(threads[i]);
    }
    
    // Calcular π
    double pi_estimate;
    if (method == 1) {
        pi_estimate = 4.0 * (double)total_inside / total_points;
    } else {
        // evitar división por cero
        if (total_inside == 0) pi_estimate = 0.0;
        else pi_estimate = (2.0 * 1.0 * total_points) / (1.0 * total_inside); // L = D = 1
    }
    
    printf("Tiempo con THREADS: %.6f segundos\n", elapsed);
    printf("Puntos dentro/cruces: %lld de %lld\n", total_inside, total_points);
    
    return pi_estimate;
}

// ==================== IMPLEMENTACIÓN CON PROCESOS (Windows API) ====================
double parallel_processes_monte_carlo(long long total_points, int num_processes, int method) {
    char map_name[MAP_NAME_MAX];
    HANDLE hMapFile = NULL;
    shared_data_t* shared = NULL;
    HANDLE hMutex = NULL;
    PROCESS_INFORMATION pi[MAX_PROCESSES];
    STARTUPINFO si;
    
    printf("\n=== INICIANDO PROCESOS (%d procesos, %lld puntos totales) ===\n", 
           num_processes, total_points);
    
    // Crear nombre único para el file mapping
    snprintf(map_name, MAP_NAME_MAX, "Local\\MonteCarloMap_%lu_%lu", 
             GetCurrentProcessId(), GetTickCount());
    
    // Crear file mapping
    hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 
        0, sizeof(shared_data_t), map_name);
    
    if (hMapFile == NULL) {
        fprintf(stderr, "Error creando file mapping: %lu\n", GetLastError());
        exit(1);
    }
    
    // Mapear memoria compartida
    shared = (shared_data_t*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shared_data_t));
    if (shared == NULL) {
        fprintf(stderr, "Error mapeando memoria: %lu\n", GetLastError());
        CloseHandle(hMapFile);
        exit(1);
    }
    
    // Crear nombre del mutex (único)
    char mutex_name[128];
    snprintf(mutex_name, sizeof(mutex_name), "Local\\MonteCarloMutex_%lu_%lu",
             GetCurrentProcessId(), GetTickCount());
    
    // Crear mutex con nombre
    hMutex = CreateMutexA(NULL, FALSE, mutex_name);
    if (hMutex == NULL) {
        fprintf(stderr, "Error creando mutex nombrado: %lu\n", GetLastError());
        UnmapViewOfFile(shared);
        CloseHandle(hMapFile);
        exit(1);
    }
    
    // Inicializar datos compartidos (incluyendo el nombre del mutex)
    shared->total_points = total_points;
    shared->points_inside = 0;
    shared->num_workers = num_processes;
    shared->method = method;
    shared->needle_length = 1.0;
    shared->line_spacing = 1.0;
    snprintf(shared->mutex_name, sizeof(shared->mutex_name), "%s", mutex_name);
    
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    // Inicializar estructura STARTUPINFO
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    
    // Crear procesos hijos
    for (int i = 0; i < num_processes; i++) {
        char cmdline[1024];
        char exe_path[MAX_PATH];
        
        // Obtener ruta del ejecutable actual
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        
        // Construir línea de comandos: exe child <worker_id> <total_points> <method> <map_name>
        snprintf(cmdline, sizeof(cmdline), "\"%s\" child %d %lld %d %s", 
                exe_path, i, total_points, method, map_name);
        
        ZeroMemory(&pi[i], sizeof(PROCESS_INFORMATION));
        
        if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi[i])) {
            fprintf(stderr, "Error creando proceso %d: %lu\n", i, GetLastError());
            continue;
        }
        
        printf("Proceso hijo %d lanzado (PID: %lu)\n", i, pi[i].dwProcessId);
    }
    
    // Esperar a que todos los procesos hijos terminen
    for (int i = 0; i < num_processes; i++) {
        if (pi[i].hProcess) {
            WaitForSingleObject(pi[i].hProcess, INFINITE);
            CloseHandle(pi[i].hProcess);
            CloseHandle(pi[i].hThread);
        }
    }
    
    QueryPerformanceCounter(&end);
    double elapsed = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
    
    // Calcular π
    double pi_estimate;
    if (method == 1) {
        pi_estimate = 4.0 * (double)shared->points_inside / total_points;
    } else {
        if (shared->points_inside == 0) pi_estimate = 0.0;
        else pi_estimate = (2.0 * shared->needle_length * total_points) / 
                     (shared->line_spacing * shared->points_inside);
    }
    
    printf("Tiempo con PROCESOS: %.6f segundos\n", elapsed);
    printf("Puntos dentro/cruces: %lld de %lld\n", shared->points_inside, total_points);
    
    // Limpiar recursos
    UnmapViewOfFile(shared);
    CloseHandle(hMapFile);
    if (hMutex) CloseHandle(hMutex);
    
    return pi_estimate;
}

// ==================== CÓDIGO PARA PROCESOS HIJOS ====================
int run_as_child_process(int argc, char* argv[]) {
    if (argc < 6) {
        fprintf(stderr, "Uso: programa child worker_id total_points method map_name\n");
        return 1;
    }
    
    int worker_id = atoi(argv[2]);
    long long total_points = atoll(argv[3]);
    int method = atoi(argv[4]);
    char* map_name = argv[5];
    
    // Abrir file mapping existente
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, map_name);
    if (hMapFile == NULL) {
        fprintf(stderr, "Error abriendo file mapping: %lu\n", GetLastError());
        return 1;
    }
    
    // Mapear memoria compartida
    shared_data_t* shared = (shared_data_t*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shared_data_t));
    if (shared == NULL) {
        fprintf(stderr, "Error mapeando memoria: %lu\n", GetLastError());
        CloseHandle(hMapFile);
        return 1;
    }
    
    // Abrir mutex por nombre (leído desde shared->mutex_name)
    HANDLE hMutexLocal = NULL;
    if (shared->mutex_name[0] != '\0') {
        hMutexLocal = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, shared->mutex_name);
        if (hMutexLocal == NULL) {
            // Si falla abrir, intentar crearlo con el mismo nombre (fallback)
            hMutexLocal = CreateMutexA(NULL, FALSE, shared->mutex_name);
            if (hMutexLocal == NULL) {
                fprintf(stderr, "Error abriendo/creando mutex en child: %lu\n", GetLastError());
                // Continuar sin sincronización no es recomendado, pero seguimos para no bloquear pruebas
                hMutexLocal = NULL;
            }
        }
    }
    
    // Ejecutar el trabajo (pasa hMutexLocal para sincronizar)
    if (method == 1) {
        dartboard_process_worker(worker_id, shared, hMutexLocal);
    } else {
        needles_process_worker(worker_id, shared, hMutexLocal);
    }
    
    // Limpiar
    if (hMutexLocal) CloseHandle(hMutexLocal);
    UnmapViewOfFile(shared);
    CloseHandle(hMapFile);
    
    return 0;
}

// ==================== VERSIÓN SERIAL ====================
double serial_monte_carlo(long long total_points, int method) {
    long long count = 0;
    unsigned int seed = GetTickCount();
    
    printf("\n=== INICIANDO VERSION SERIAL (%lld puntos) ===\n", total_points);
    
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    if (method == 1) {
        // Dartboard
        for (long long i = 0; i < total_points; i++) {
            double x = rand_double_win(&seed);
            double y = rand_double_win(&seed);
            
            if (x * x + y * y <= 1.0) {
                count++;
            }
        }
    } else {
        // Needles
        for (long long i = 0; i < total_points; i++) {
            double center_y = rand_double_win(&seed) * 1.0;
            double angle = rand_double_win(&seed) * 3.14159265358979323846;
            
            double half_length = 0.5;
            double y_min = center_y - half_length * sin(angle);
            double y_max = center_y + half_length * sin(angle);
            
            if (y_min <= 0.0 || y_max >= 1.0) {
                count++;
            }
        }
    }
    
    QueryPerformanceCounter(&end);
    double elapsed = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
    
    double pi_estimate;
    if (method == 1) {
        pi_estimate = 4.0 * (double)count / total_points;
    } else {
        if (count == 0) pi_estimate = 0.0;
        else pi_estimate = (2.0 * 1.0 * total_points) / (1.0 * count);
    }
    
    printf("Tiempo SERIAL: %.6f segundos\n", elapsed);
    printf("Puntos dentro/cruces: %lld de %lld\n", count, total_points);
    
    return pi_estimate;
}

// ==================== FUNCIONES AUXILIARES ====================
void print_results(double computed_pi, double actual_pi, long long iterations, 
                  double time_taken, const char* method) {
    double error = fabs(computed_pi - actual_pi);
    double relative_error = (error / actual_pi) * 100;
    
    printf("\n=== RESULTADOS %s ===\n", method);
    printf("Pi calculado:  %.10f\n", computed_pi);
    printf("Pi real:       %.10f\n", actual_pi);
    printf("Error absoluto: %.2e\n", error);
    printf("Error relativo: %.6f%%\n", relative_error);
    printf("Iteraciones:   %lld\n", iterations);
    printf("Tiempo:        %.6f segundos\n", time_taken);
    if (time_taken > 0) {
        printf("Iteraciones/segundo: %.0f\n", iterations / time_taken);
    }
    printf("===================================\n");
}

void benchmark_method(long long points, int method, const char* method_name) {
    const double ACTUAL_PI = 3.14159265358979323846;
    
    printf("\n\n**************************************************");
    printf("\n* BENCHMARK: %s", method_name);
    printf("\n**************************************************");
    
    // Serial
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    
    QueryPerformanceCounter(&start);
    double pi_serial = serial_monte_carlo(points, method);
    QueryPerformanceCounter(&end);
    double time_serial = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
    
    print_results(pi_serial, ACTUAL_PI, points, time_serial, "SERIAL");
    
    // Threads (2, 4, 8)
    int thread_counts[] = {2, 4, 8};
    for (int i = 0; i < 3; i++) {
        QueryPerformanceCounter(&start);
        double pi_threads = parallel_threads_monte_carlo(points, thread_counts[i], method);
        QueryPerformanceCounter(&end);
        double time_threads = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
        
        const char* labels[3] = {"2 THREADS", "4 THREADS", "8 THREADS"};
        print_results(pi_threads, ACTUAL_PI, points, time_threads, labels[i]);
        printf("Speedup: %.2fx\n", time_serial / time_threads);
    }
    
    // Procesos (2, 4)
    int process_counts[] = {2, 4};
    for (int i = 0; i < 2; i++) {
        QueryPerformanceCounter(&start);
        double pi_processes = parallel_processes_monte_carlo(points, process_counts[i], method);
        QueryPerformanceCounter(&end);
        double time_processes = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
        
        const char* plabels[2] = {"2 PROCESOS", "4 PROCESOS"};
        print_results(pi_processes, ACTUAL_PI, points, time_processes, plabels[i]);
        printf("Speedup: %.2fx\n", time_serial / time_processes);
    }
}

// ==================== PROGRAMA PRINCIPAL ====================
// ==================== PROGRAMA PRINCIPAL ====================
int main(int argc, char* argv[]) {
    // Si se ejecuta como proceso hijo
    if (argc >= 2 && strcmp(argv[1], "child") == 0) {
        return run_as_child_process(argc, argv);
    }

    while (1) {
        long long points;
        int choice;

        printf("\n=========================================\n");
        printf("=== CALCULO DE PI - PARALELISMO WINDOWS ===\n");
        printf("Seleccione metodo:\n");
        printf("1. Benchmark completo Dartboard\n");
        printf("2. Benchmark completo Needles\n");
        printf("3. Ejecucion simple\n");
        printf("4. Salir\n");
        printf("Opcion: ");
        if (scanf("%d", &choice) != 1) return 1;

        if (choice == 4) {
            printf("Saliendo del programa...\n");
            break;  // salimos del bucle y termina el programa
        }

        if (choice == 3) {
            printf("Ingrese numero de puntos: ");
            if (scanf("%lld", &points) != 1) return 1;

            printf("Seleccione metodo:\n");
            printf("1. Dartboard\n");
            printf("2. Needles\n");
            printf("Opcion: ");
            int method;
            if (scanf("%d", &method) != 1) return 1;

            printf("Seleccione implementacion:\n");
            printf("1. Serial\n");
            printf("2. Threads (4)\n");
            printf("3. Procesos (4)\n");
            printf("Opcion: ");
            int impl;
            if (scanf("%d", &impl) != 1) return 1;

            const double ACTUAL_PI = 3.14159265358979323846;
            double pi_result;

            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&start);

            switch (impl) {
                case 1:
                    pi_result = serial_monte_carlo(points, method);
                    break;
                case 2:
                    pi_result = parallel_threads_monte_carlo(points, 4, method);
                    break;
                case 3:
                    pi_result = parallel_processes_monte_carlo(points, 4, method);
                    break;
                default:
                    printf("Opcion no valida!\n");
                    continue;
            }

            QueryPerformanceCounter(&end);
            double elapsed = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;

            print_results(pi_result, ACTUAL_PI, points, elapsed,
                (impl == 1) ? "SERIAL" : (impl == 2) ? "THREADS" : "PROCESOS");

        } else if (choice == 1 || choice == 2) {
            printf("Ingrese numero de puntos para benchmark: ");
            if (scanf("%lld", &points) != 1) return 1;

            if (choice == 1) {
                benchmark_method(points, 1, "DARTBOARD");
            } else {
                benchmark_method(points, 2, "NEEDLES");
            }
        } else {
            printf("Opcion no valida, intenta de nuevo.\n");
        }

    }

    return 0;
}

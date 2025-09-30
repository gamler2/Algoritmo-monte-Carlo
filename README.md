# Algoritmo-monte-Carlo
El código es un entorno de benchmarking para experimentos de Monte Carlo en Windows, que mide cómo el uso de hilos y procesos influye en la velocidad y exactitud al calcular el número π mediante los métodos de Dartboard y Buffon’s Needle.
El código completo implementa un programa en C para estimar el valor de π (pi) usando el método de Monte Carlo, con tres modalidades de ejecución:

Versión Serial → Todo se ejecuta en un solo bucle, sin paralelismo.

Versión con Threads (hilos) → Divide el trabajo entre varios hilos, cada uno genera una parte de los puntos y luego combina los resultados.

Versión con Procesos → Crea procesos independientes que comparten resultados mediante memoria compartida y sincronización con mutex.

🔹 Métodos de cálculo disponibles:

Dartboard (tiro de dardos): Genera puntos aleatorios dentro de un cuadrado y cuenta cuántos caen dentro del círculo inscrito. La proporción permite estimar π.

Needles (agujas de Buffon): Lanza agujas de longitud fija sobre un plano con líneas paralelas y mide la probabilidad de que crucen una línea, lo que también permite estimar π.

🔹 Estructura del código:

Generador de números aleatorios propio (lineal congruencial) para consistencia.

Funciones trabajadoras (workers): cada hilo o proceso ejecuta uno de los métodos (Dartboard o Needles).

Funciones de control: gestionan la creación de hilos/procesos, esperan su finalización y suman los resultados.

Funciones de utilidad: cálculo de errores, impresión de resultados y benchmarks.

Menú principal: permite al usuario elegir entre benchmarking (con serial, 2/4/8 threads, 2/4 procesos) o una ejecución personalizada.

🔹 Objetivo del programa:

Mostrar cómo varía el rendimiento y la precisión al estimar π usando distintas técnicas de paralelismo (threads vs procesos) en Windows.

Permitir comparar el tiempo de ejecución y calcular speedup (aceleración relativa respecto al modo serial).

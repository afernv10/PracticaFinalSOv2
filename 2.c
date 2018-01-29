#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>

/************************************************************
// Entregada parte obligatoria, en los ultimos puntos hay metodos y códigos comentados, están así debido a que hacían cosas que 
// no eran propias o simplemente no salía nada. Así es el caso de el mensaje de cuando se mata al de la fuente, este solo sale
// cuando se manda una señal aislada. Para la espera de los hilos que estén compitiendo también está comentado una prueba,
// tratamos de utilizar otras cosas como el join o más variables condición pero se quedaba colgado.
************************************************************/

//#define ATLETAS_MAX 10
//#define TARIMAS_TOT 2
int numeroAtletas;
int numeroTarimas;

pthread_mutex_t semaforoColaAtletas;
pthread_mutex_t semaforoFicheroLog;
pthread_mutex_t semaforoPodio;

pthread_mutex_t semaforoFuente;
pthread_cond_t condicionFuente;

int contadorAtletas;

struct cola_Atletas {
	
	int idAtleta;
	int ha_competido;
	int tarima_asignada;
	int necesita_beber;
	pthread_t hiloAtleta;	/* Hilo de cada atleta */	
};

/* Lista de 10 atletas máximo */
struct cola_Atletas *atletas;


/* Fichero Log */
FILE *fichero;
const char *NOMBRE_FICHERO = "registroTiempos.log";

/* Podio */
struct podio{
	int idAtleta;
	int puntos;
};
struct podio campeones[3];

/* Para saber si la fuente está ocupada */
int fuenteOcupada;

int finalizar;//Cuando cambia a 1 termina el programa	// podemos realizar una manejadora a la que le enviemos una senal especifica

void nuevoCompetidor(int sig);			/* Actuará como manejadora, y crea el hilo del Atleta */
void *accionesAtleta(void *idAtleta);	/* Función hilo de los atletas */
void *accionesTarima(void *tarima_asignada);	/* Función hilo de las tarimas */
void writeLogMessage(char *id, char *msg);
int calculaAleatorios(int min, int max);
int aleatorioSalud(int probabilidad, int semilla);	/* Calcula el aleatorio para casos concretos de 1 probabilidad */
int sacarPosicion(int identificadorAtleta);
int aleatorioLevantamiento(int valido, int nuloNormas, int nuloFuerza);
void finalizarPrograma();
void levantamiento(int atletaId, int tarimaID);
int haySitioEnCola();
int noHayNadieEnCola();
void asignaPtsPodio(int id, int puntos, int atletasQueCompitieron);
void *fuente();
int buscaMejorPosicion(int puntos);
void actualizaPodio(int id, int puntos);
void mostrarEstadisticas();
void liberarCola(int posicion);



int main(int argc, char *argv[]) {


	srand(getpid()); /* semilla para el cálculo de aleatorios */

	printf("\n******** Bienvenido a la competición ********\n");
	printf("El PID correspondiente a esta ejecución es: %d\n", getpid());
	printf("- Introduce kill -10 PID para mandar al atleta a la 1.\n");
	printf("- Introduce kill -12 PID para mandar al atleta a la 2.\n");
	printf("- Introduce kill -13 PID para mostrar estadísticas de la competición.\n");

	/* Comprobamos si el número de argumentos pasado es correcto */
	if(argc != 3){
		printf("Argumentos erróneos.\t Sintaxis: ./ejecutable numeroAtletas numeroTarimas\n");
		return -1;	
	}

	numeroAtletas = atoi(argv[1]);
	atletas = (struct cola_Atletas*) malloc(sizeof(struct cola_Atletas) * numeroAtletas);
	numeroTarimas = atoi(argv[2]);
	
	/* Preparamos las senales que podemos recibir */
	if(signal(SIGUSR1,nuevoCompetidor) == SIG_ERR){
		perror("Error en la creación de nuevo competidor para la tarima 1.\n");
		exit(-1);
	}
	if(signal(SIGUSR2,nuevoCompetidor) == SIG_ERR){
		perror("Error en la creación de nuevo competidor para la tarima 2.\n");
		exit(-1);
	}
	if(signal(SIGINT, finalizarPrograma) == SIG_ERR){
		perror("Error al recibir la señal para finalizar la competición.");
		exit(-1);
	}
	if(signal(SIGPIPE, mostrarEstadisticas) == SIG_ERR){
		perror("Error al recibir la señal para mostrar las estadísticas.");
		exit(-1);
	}

	/******* Inicializamos recursos *******/

	/* Inicializamos semáforos */
	if (pthread_mutex_init(&semaforoColaAtletas,NULL) != 0) exit(-1);
	if (pthread_mutex_init(&semaforoFicheroLog, NULL) != 0) exit(-1);
	if (pthread_mutex_init(&semaforoPodio,NULL) != 0) exit(-1);
	if (pthread_mutex_init(&semaforoFuente,NULL) != 0) exit(-1);
	if (pthread_cond_init(&condicionFuente, NULL) !=0) exit(-1);

	
	/* Inicializamos el contador de atletas */
	contadorAtletas = 0;

	/* Inicializamos el flag de finalizar */
	finalizar = 0;	// 1 si finalizamos

	/* Inicializamos el fuenteOcupada */
	fuenteOcupada = 0;	// ira contando cuantos han pasado por ella y cuando haya 2 o multiplo de 2 bebe 1 y sale

	/* Inicializamos la lista de atletas */
	int i;
	pthread_mutex_lock(&semaforoColaAtletas);
	for(i = 0; i<numeroAtletas; i++){

		/* Tenemos que inicializar cada uno de los componentes de struct de atletas */

		/* Inicializamos ids de atletas */
		atletas[i].idAtleta = 0;
		/* Inicializamos si ha_competido o no;  NO COMPETIDO y espera en cola = 0; ESTA SIENDO ATENDIDO = 1; FINALIZADO = 2 */	
		atletas[i].ha_competido = 0;
		/* Inicializamos la tarima a la que va (1 o 2)*/
		atletas[i].tarima_asignada = 0;
		/* Inicializamos flag de si necesita beber o no */
		atletas[i].necesita_beber = 0;
	}
	pthread_mutex_unlock(&semaforoColaAtletas);
	
	// inicializamos podio
	pthread_mutex_lock(&semaforoPodio);
	for(i = 0; i<3; i++){
		/* Tenemos que inicializar cada uno de los componentes de struct de podio */
		// Inicializamos ids de atletas 
		campeones[i].idAtleta = 0;
		// Inicializamos puntos 	
		campeones[i].puntos = 0;
	}
	pthread_mutex_unlock(&semaforoPodio);

	

	/* Inicializamos fichero log */
	/* w por si no existiera el fichero registroTiempos para crearlo */
	pthread_mutex_lock(&semaforoFicheroLog);
	fichero = fopen("registroTiempos.log", "w");
	fclose(fichero);
	pthread_mutex_unlock(&semaforoFicheroLog);

	/* guardar registro de comienzo de competicion en log */
	char info[100];
	char campeonato1[100];
	sprintf(info, "  INFO ");
	sprintf(campeonato1, "La competición acaba de comenzar, pueden llegar ya atletas.");
	writeLogMessage(info, campeonato1);
	
	pthread_t hiloTarima[numeroTarimas];

	/* Creamos los 2 hilos de tarimas */
	for( i = 1; i<=numeroTarimas; i++){
		
		pthread_create(&hiloTarima[i-1], NULL, accionesTarima, (void *) &i);
		sleep(1);
	}

	/* Esperamos señal sigusr1 o sigusr2 */
	//bool inf = true;
	/* bucle infinito para esperar una de ellas */
	while(finalizar==0){//mientras finalizar sea 0 bucle infinito para esperar las señales
		pause();	/* con pause en el momento que recibimos una vamos a la manejadora */
	}

	char podio[10];
	sprintf(podio, " PODIO ");
	writeLogMessage(info, podio);

	char oroID[100];
	sprintf(oroID, "atleta_%d ",campeones[0].idAtleta);
	char oroPts[100];
	sprintf(oroPts, "Medalla de Oro - %d pts.",campeones[0].puntos );
	writeLogMessage(oroID, oroPts);

	char plataID[100];
	sprintf(plataID, "atleta_%d ",campeones[1].idAtleta);
	char plataPts[100];
	sprintf(plataPts, "Medalla de Plata - %d pts.",campeones[1].puntos);
	writeLogMessage(plataID, plataPts);

	char bronceID[100];
	sprintf(bronceID, "atleta_%d ",campeones[2].idAtleta);
	char broncePts[100];
	sprintf(broncePts, "Medalla de Bronce - %d pts.",campeones[2].puntos);
	writeLogMessage(bronceID, broncePts);

	
	char cierre[100];
	sprintf(cierre, "La competición ha terminado, todos se han ido ya.");
	writeLogMessage(info, cierre);

	return 0;
}

void nuevoCompetidor (int sig){
	//Se vuelve a asignar la función a la llamada
	if(signal(SIGUSR1,nuevoCompetidor) == SIG_ERR){
		perror("Error en la creación de nuevo competidor para la tarima 1.\n");
		exit(-1);
	}
	if(signal(SIGUSR2,nuevoCompetidor) == SIG_ERR){
		perror("Error en la creación de nuevo competidor para la tarima 2.\n");
		exit(-1);
	}

	char info[10];
	sprintf(info, "INFO ");
	char noEntra[100]; 

	int sitio;
	pthread_mutex_lock(&semaforoColaAtletas);
	sitio = haySitioEnCola();
	pthread_mutex_unlock(&semaforoColaAtletas);
	if(sitio == -1){
		sprintf(noEntra, "Ha llegado un nuevo atleta pero no hay hueco, se va.");
		writeLogMessage(info, noEntra);
	
	} else if(sitio != -1){
		pthread_mutex_lock(&semaforoColaAtletas);
		contadorAtletas++;
		atletas[sitio].idAtleta = contadorAtletas;
		pthread_mutex_unlock(&semaforoColaAtletas);
		
		if(sig==SIGUSR1){
			pthread_mutex_lock(&semaforoColaAtletas);
			atletas[sitio].tarima_asignada = 1;
			pthread_mutex_unlock(&semaforoColaAtletas);
			
				//si recibimos SIGUSR2:
		} else if(sig==SIGUSR2){
			pthread_mutex_lock(&semaforoColaAtletas);
			atletas[sitio].tarima_asignada = 2;
			pthread_mutex_unlock(&semaforoColaAtletas);
		}
		pthread_create(&atletas[sitio].hiloAtleta,NULL,accionesAtleta,(void *) &atletas[sitio].idAtleta);
	}

}

int haySitioEnCola(){
	int x;
	for(x = 0; x < numeroAtletas; x++){
		if(atletas[x].idAtleta == 0){
			return x; //Encuentra posición libre
		}
	}
	return -1; //No hay sitio libre
}

// Mata al que está en la fuente 			// ERROR no imprime el log
int noHayNadieEnCola(){
	int x;
	for(x = 0; x < numeroAtletas; x++){
		if(atletas[x].idAtleta != 0 && atletas[x].necesita_beber == 1 ){
			char matar[100];
			sprintf(matar, "Me he quedado en la fuente colgado sin beber y me han matado.");
			char muerto[100];
			sprintf(muerto, "atleta_%d",atletas[x].idAtleta);
			printf("dead %d\n", x);
			writeLogMessage(muerto,matar);
			pthread_cancel(atletas[x].hiloAtleta);
			pthread_mutex_lock(&semaforoColaAtletas);
			atletas[x].idAtleta = 0;
			atletas[x].necesita_beber = 0;
			pthread_mutex_unlock(&semaforoColaAtletas);
		} else if (atletas[x].idAtleta != 0 && atletas[x].necesita_beber == 0){
			printf("ajoin %d\n", x);
			//pthread_join(atletas[x].hiloAtleta, NULL);
			printf("djoin %d\n", x);
			return -1;
		}
	}
}


/********************************************************************************/
/***************************** ACCIONES ATLETA **********************************/
/********************************************************************************/

void *accionesAtleta (void *idAtleta ) { /*Acciones de los atletas en el circuito */
	
	/* Pasamos a entero el puntero *idAtleta */
	int identificadorAtleta = *(int*)idAtleta;
	pthread_mutex_lock(&semaforoColaAtletas);
	int posicion = sacarPosicion(identificadorAtleta);
	pthread_mutex_unlock(&semaforoColaAtletas);

	
	/* Guardar en el log la hora de entrada del atleta en la cola, en la tarima no ha entrado todavía */
	char entradaAtleta [100];
	sprintf (entradaAtleta, "Entra en la cola.");
	char numeroAtleta [100];
	sprintf(numeroAtleta, "atleta_%d ", identificadorAtleta);
	writeLogMessage(numeroAtleta, entradaAtleta);

	/* Guardar en el log el tipo de tarima al que accede */
	char accesoTarima1[100];
	char accesoTarima2[100];
	sprintf(accesoTarima1, "El atleta entra a la cola para acceder a tarima 1.");
	sprintf(accesoTarima2, "El atleta entra a la cola para acceder a tarima 2.");

	char nosubeTarima[100];
	sprintf(nosubeTarima, "No llega a subir a la tarima por problemas de salud.");

	pthread_mutex_lock(&semaforoColaAtletas);
	switch (atletas[posicion].tarima_asignada) {
		
		/* En caso de que entre a la cola de tarima 1 */
		case 1: writeLogMessage(numeroAtleta, accesoTarima1); 
		break;
		/* En caso de que entre a la cola de la tarima 2 */
		case 2: writeLogMessage(numeroAtleta, accesoTarima2);
		break;
	}
	pthread_mutex_unlock(&semaforoColaAtletas);
	
	// espera para ver si compite
	sleep(3);

	while(1){
		
		if(atletas[posicion].ha_competido == 0){
		
			int alea = aleatorioSalud(15, identificadorAtleta);
				
			if(alea == 1){
				// atleta tiene problemas de deshidratación y electrolitos y abandona cola
				// Abandona la cola de la tarima y se escribe en el log 
					
				writeLogMessage(numeroAtleta, nosubeTarima);
				//pthread_mutex_lock(&semaforoColaAtletas);
				// Se libera espacio en la cola 
				pthread_mutex_lock(&semaforoColaAtletas);
				liberarCola(posicion);
				pthread_mutex_unlock(&semaforoColaAtletas);
				// Se da fin al hilo 
				pthread_exit(NULL);
			} else {
				sleep(3);
			}
		
		} else if(atletas[posicion].ha_competido == 2){
			// liberamos espacio en la cola del que se va
			pthread_mutex_lock(&semaforoColaAtletas);
			liberarCola(posicion);
			pthread_mutex_unlock(&semaforoColaAtletas);
			pthread_exit(NULL);
		}
	}

	/*	// Comprueba si tiene que beber agua o tiene algun problema los atletas que estén en la cola cada 3 segundos 
	pthread_mutex_lock(&semaforoColaAtletas);
	while(atletas[posicion].ha_competido == 0) {
		pthread_mutex_unlock(&semaforoColaAtletas);
		pthread_mutex_lock(&semaforoColaAtletas);
		if(atletas[posicion].ha_competido == 0){	
			//calculamos el aleatorio de la probabilidad del 15% de irse por problemas
				// 1 si SI, 0 si NO 
			int alea = aleatorioSalud(15, identificadorAtleta);

			if(alea == 1){
				// atleta tiene problemas de deshidratación y electrolitos y abandona cola 
				// Abandona la cola de la tarima y se escribe en el log 
				
				writeLogMessage(numeroAtleta, nosubeTarima);

				//pthread_mutex_lock(&semaforoColaAtletas);
				// Se libera espacio en la cola 
				atletas[posicion].necesita_beber = 0;
				atletas[posicion].idAtleta = 0;
				atletas[posicion].tarima_asignada = 0;
				atletas[posicion].ha_competido = 0;
				pthread_mutex_unlock(&semaforoColaAtletas);
				// Se da fin al hilo
				pthread_exit(NULL);	
				
			}else if (alea == 0) {	// Si no se tienen problemas se esperan 3 segs para volver a comprobar 
				sleep(3);
			}
			pthread_mutex_lock(&semaforoColaAtletas);
		}
		pthread_mutex_unlock(&semaforoColaAtletas);	
	}
	pthread_mutex_unlock(&semaforoColaAtletas);
	
	// Cuando el atleta haya competido podemos avanzar, le esperamos 
	pthread_mutex_lock(&semaforoColaAtletas);
	while(atletas[posicion].ha_competido==1) {
		// aquí el atleta está compitiendo
		// esperamos a que cambie el flag
		pthread_mutex_unlock(&semaforoColaAtletas);
	}
	pthread_mutex_unlock(&semaforoColaAtletas);
	
	printf("elatleta_%d ha pasado el while de su metodo\n", atletas[posicion].idAtleta);
	
	// liberamos espacio en la cola del que se va
	pthread_mutex_lock(&semaforoColaAtletas);
	liberarCola(posicion);
	pthread_mutex_unlock(&semaforoColaAtletas);

	printf("va a dar exit atleta_%d\n", identificadorAtleta);
	// Se da fin al hilo 
	pthread_exit(NULL);*/				
}

void liberarCola(int posicion){
	atletas[posicion].idAtleta = 0;
	atletas[posicion].necesita_beber = 0;
	atletas[posicion].tarima_asignada = 0;
	atletas[posicion].ha_competido = 0;
}

int siguienteAtletaAtender(int identificadorTarima){
	int posicionSiguienteAtleta = -1;
	int idMasBajo = contadorAtletas;
	int i;
	pthread_mutex_lock(&semaforoColaAtletas);
	for(i = 0; i < numeroAtletas; i++){
		 if(atletas[i].idAtleta <= idMasBajo &&  atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0 && atletas[i].tarima_asignada == identificadorTarima){
			   posicionSiguienteAtleta = i;
			   idMasBajo = atletas[i].idAtleta;
		  }
	}
	if(posicionSiguienteAtleta == -1){	// no hay de su tarima, cogemos de la otra
		for(i = 0; i < numeroAtletas; i++){
		 if(atletas[i].idAtleta <= idMasBajo &&  atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0 && atletas[i].tarima_asignada != identificadorTarima){
			   posicionSiguienteAtleta = i;
			   idMasBajo = atletas[i].idAtleta;
		  }
		}
	}
	if(posicionSiguienteAtleta != -1){	
		clientes[posicionSiguienteAtleta].ha_competido = 1;
	}
	pthread_mutex_unlock(&semaforoColaAtletas);
	return posicionSiguienteAtleta;
}

/********************************************************************************/
/***************************** ACCIONES TARIMA **********************************/
/********************************************************************************/

void *accionesTarima(void *tarima_asignada){

	/* Convertimos a int el puntero de idTarima que pasamos */
	int identificadorTarima = *(int*) tarima_asignada;
	
	int competidos = 0;
	int puntos;

	/* Nos guardamos el nombre para el log */
	char nTarima[100];
	sprintf(nTarima, "tarima_%d ", identificadorTarima);	//???????? revisar los indices
	char ayudaTarima[100];

	char juez[100];
	sprintf(juez, "tarima_%d",identificadorTarima);
	char descansa1[100];
	sprintf(descansa1,"El juez empieza su descanso.");
	char descansa2[100];
	sprintf(descansa2, "El juez vuelve a la tarima.");
	
	int posicionSiguienteAtleta;

	int atletaElegido;
	/* Decimos que la tarima está libre */
	int ocupada = 0;
	printf("ocupada t%d %d", identificadorTarima, ocupada);
	int TRUE = 1;

	while(TRUE){

		pthread_mutex_lock(&semaforoColaAtletas);
		if(competidos%4 == 0 && contadorAtletas != 0){

			ocupada = 1;
			writeLogMessage(juez, descansa1);
			sleep(10);
			writeLogMessage(juez, descansa2);
			ocupada = 0;
			pthread_mutex_unlock(&semaforoColaAtletas);
		}
		posicionSiguienteAtleta = siguienteAtletaAtender(identificadorTarima);
		if(posicionSiguienteAtleta != -1){
			ocupada = 1;

			levantamiento(atletaElegido, posAtleta);

			// ha realizado el levantamiento en sí, espera a que el juez le diga si tiene que beber
			char beber[100];
			sprintf(beber, "El juez le manda beber agua.");
			char atleta[100];
			sprintf(atleta, "atleta_%d ",atletas[posicionSiguienteAtleta].idAtleta);
			// El juez le manda ir a beber agua independientemente del caso con 10% de probabilidad
			int aBeber = aleatorioSalud(90, atletaElegido);
			if(aBeber == 1){

			}		

		}
		sleep(1);

	}

	////////////////////////////////////////a partir de aqui antiguo

	/* buscamos un atleta, el que más tiempo lleve esperando, prioridad: su tarima */
	while(TRUE){
		
		int i;
		int posAtleta = 0, variable = 999;		// esta variable nos sirve para identificar quien ha estado esperando más en la cola
		/* Mientras la tarima no esté ocupada */
		while(ocupada == 0){

			/* bloqueamos semaforo, solo puede entrar uno a la vez a tarima */
			pthread_mutex_lock(&semaforoColaAtletas);

			
			/* buscamos atletas */
			for (i = 0; i < numeroAtletas; i++){
				/* miramos primero que sea de esta tarima*/ // no se si falta lo de necesitabeber...
				if(atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0 && atletas[i].tarima_asignada == identificadorTarima){
					
					if(atletas[i].idAtleta < variable){
							
						atletaElegido = atletas[i].idAtleta;
						posAtleta = i;
						atletas[posAtleta].ha_competido = 1;
						
						/* el atleta entra a tarima */
						ocupada = 1;
						variable = atletas[i].idAtleta;
						pthread_mutex_unlock(&semaforoColaAtletas);	// este era el original
					}
				}
				// pthread_mutex_unlock(&semaforoColaAtletas);	// este tb iba
			}
			//pthread_mutex_unlock(&semaforoColaAtletas);
			
			if(ocupada == 0){
				//pthread_mutex_lock(&semaforoColaAtletas);	// poniendo unlock tb iba
				/* ahora buscamos de otra tarima que no sea la prioritaria */
				for (i = 0; i < numeroAtletas; i++){
					/* miramos primero que sea de esta tarima*/ // no se si falta lo de necesitabeber...
					if(atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0){
						
						if(atletas[i].idAtleta < variable){
							sprintf(ayudaTarima, "Entra el atleta_%d de la otra cola para avanzar.",atletas[i].idAtleta);
							writeLogMessage(nTarima, ayudaTarima);	
							atletaElegido = atletas[i].idAtleta;
							posAtleta = i;
							atletas[posAtleta].ha_competido = 1;
							posAtleta = i;
							ocupada = 1;
							/* el atleta entra a tarima */
							pthread_mutex_unlock(&semaforoColaAtletas);	// este era el original
						}
					}
				}
				// pthread_mutex_unlock(&semaforoColaAtletas);
			}
			// en el caso de que no llegue ningun atleta a ninguna cola tb tenemos que unlock
			pthread_mutex_unlock(&semaforoColaAtletas);
			
			if(ocupada == 0){
				sleep(1);
			}
			printf("ocupada t%d %d", identificadorTarima, ocupada);
		}

		/* realizamos el levantamiento */
		printf("posAtleta antes de lev %d\n", posAtleta);
		levantamiento(atletaElegido, posAtleta);

		// ha realizado el levantamiento en sí, espera a que el juez le diga si tiene que beber
		char beber[100];
		sprintf(beber, "El juez le manda beber agua.");
		char atleta[100];
		sprintf(atleta, "atleta_%d ",atletaElegido);
		int a1 = atletaElegido;
		// El juez le manda ir a beber agua independientemente del caso con 10% de probabilidad
		int aBeber = aleatorioSalud(90, atletaElegido);

		if(aBeber == 1){

			// al entrar en la fuente la tarima ya no estaría ocupada
			ocupada = 0;
			int trasFin;
			pthread_mutex_lock(&semaforoFuente);
			
			// codigo a beber
			atletas[posAtleta].idAtleta = 0; 
			atletas[posAtleta].necesita_beber = 1;
			writeLogMessage(atleta, beber);
			if((++fuenteOcupada)>=2){
				
				if(finalizar ==1){
					trasFin = atletaElegido;
					printf("if f %d\n", trasFin);
				}
				pthread_cond_signal(&condicionFuente);
				/*if(finalizar == 1){
					char matar[100];
					sprintf(matar, "Me he quedado en la fuente colgado sin beber y me han matado");
					writeLogMessage(numAtleta,matar);
					printf("aquiaqui\n");
					pthread_cancel(atletas[atletaID].hiloAtleta);
				}*/
				
			}
			pthread_cond_wait(&condicionFuente, &semaforoFuente);
			char estaBebiendo[100];
			sprintf(estaBebiendo, "El atleta está bebiendo ya porque le ayuda MAL el atleta_%d", trasFin);
			
			printf("fuente quien hay: atleta_%d, pos %d\n", atletas[posAtleta].idAtleta, posAtleta);
			// liberamos de la cola
			atletas[posAtleta].ha_competido = 2;
			atletas[posAtleta].idAtleta = 0;
			atletas[posAtleta].necesita_beber = 0;
			writeLogMessage(atleta,estaBebiendo);
			pthread_mutex_unlock(&semaforoFuente);
			
		}
		


		/* flag de ha_competido finalizado*/
		// ponemos el semaforo por si hubiera entrado otro atleta y estuviera 'arriba' compitiendo
		pthread_mutex_lock(&semaforoColaAtletas);	
		atletas[posAtleta].ha_competido=2;
		pthread_mutex_unlock(&semaforoColaAtletas);	

		/* incrementamos el contador de atletas que han competido */
		competidos++;
	

		
		

		/* cada 4 descanso de jueces */
		if(competidos % 4 == 0){
			//pthread_mutex_lock(&semaforoColaAtletas);
			ocupada = 1;
			writeLogMessage(juez, descansa1);
			sleep(10);
			writeLogMessage(juez, descansa2);
			//pthread_mutex_unlock(&semaforoColaAtletas);
		}
		ocupada = 0;
		
		// si se le ha dado a finalizar comprobamos si no hay nadie 			// ERROR no iba
		/*if(finalizar == 2){
			char numeroCompes[100];
			sprintf(numeroCompes, "Numero de competidores que han pasado:%d",competidos);
			writeLogMessage(nTarima, numeroCompes);
			TRUE = 0;
		}
		// si hay alguien seguimos en bucle
		
		/* aqui volvemos a buscar... bucle infinito */
	}	

										// no habia atletas en cola y se dio a finalizar
	pthread_exit(NULL);
}




/********************************************************************************/
/*************************** FUNCIONES AUXILIARES *******************************/
/********************************************************************************/

/* Calculamos el comportamiento para si tiene problemas de salud y se va de la cola y
* para cuando el juez decide mandarle beber o no */
int aleatorioSalud(int probabilidad, int semilla){
	srand(time(NULL) * semilla);
	int numbers[100];
	int i, num, sol;
	/* si sale 1 se hace la opcion que queremos */
		// tiene problemas y sale de cola y de compe
		// juez le manda ir a beber agua
	for (i = 0; i < probabilidad; i++){
		numbers[i] = 1;
	}
    for(i = probabilidad; i < 100; i++){
    	numbers[i] = 0;
    }

    num = calculaAleatorios(0,99);
    sol = numbers[num];
    return sol;
}

/* Aleatorio para los posibles casos de levantamiento:
*	opc 1 = 80% movimiento valido
*	opc 2 = 10% nulo por normas
*	opc 3 = 10% nulo por fuerza
*/
int aleatorioLevantamiento(int valido, int nuloNormas, int nuloFuerza){
	int numbers[100];
	int i, num, caso;
	/* si sale 1 se hace la opcion que queremos */
		// tiene problemas y sale de cola y de compe
		// juez le manda ir a beber agua
	for (i = 0; i < valido; i++){
		numbers[i] = 1;
	}
    
    // creacion de posibilidades para nulo de normas
    for(i = valido; i < valido+nuloNormas; i++){
    	numbers[i] = 2;
    }

    int limiteSuperior3 = valido+nuloNormas+nuloFuerza;
    // creacion de posibilidades para nulo de fuerzas
    for(i = valido+nuloNormas; i < limiteSuperior3; i++){
    	numbers[i] = 3;
    }

    num = calculaAleatorios(0,99);
    caso = numbers[num];
    return caso;
}

void levantamiento(int atletaID, int posAtleta){
	printf("atletaId %d, [%d], t%d\n", atletaID, posAtleta, atletas[posAtleta].tarima_asignada);
	int opc, aBeber, tiempo, puntos;
	opc = aleatorioLevantamiento(80, 10, 10);
	char numAtleta[100];
	sprintf(numAtleta, "atleta_%d ", atletaID);
	char inicioLevantamiento[100];
	sprintf(inicioLevantamiento, "Comienza a realizar el levantamiento");
	
	writeLogMessage(numAtleta, inicioLevantamiento);

	char motivoFin[100];

	switch(opc){
		case 1:	/* movimiento válido */
			
			puntos = calculaAleatorios(60,300);
			sprintf(motivoFin, "Resultado: Movimiento valido - puntuacion:%d.", puntos);
			tiempo = calculaAleatorios(2,6);
			sleep(tiempo);
			break;
		case 2:	/* nulo por normas */
			puntos = 0;
			sprintf(motivoFin, "Resultado: Movimiento nulo por normas - puntuacion:%d", puntos);
			tiempo = calculaAleatorios(1,4);
			sleep(tiempo);
			break;
		case 3:	/* nulo por fuerzas */
			puntos = 0;
			sprintf(motivoFin, "Resultado: Movimiento nulo por fuerzas - puntuacion:%d", puntos);
			tiempo = calculaAleatorios(6,10);
			sleep(tiempo);
			break; 
	}
	char finLevantamiento[100];
	sprintf(finLevantamiento, "Fin del levantamiento, ha tardado %d segundos", tiempo);
	writeLogMessage(numAtleta, finLevantamiento);
	writeLogMessage(numAtleta, motivoFin);
	
	// asignamos los puntos en el podio
	pthread_mutex_lock(&semaforoPodio);
	actualizaPodio(atletaID, puntos);	
	pthread_mutex_unlock(&semaforoPodio);	

/*
	char beber[100];
	sprintf(beber, "El juez le manda beber agua.");
	/* El juez le manda ir a beber agua independientemente del caso con 10% de probabilidad
	aBeber = aleatorioSalud(10, atletaID);
	if(aBeber == 1){
		pthread_mutex_lock(&semaforoFuente);
		
		/* codigo a beber 
		atletas[posAtleta].necesita_beber = 1;
		writeLogMessage(numAtleta, beber);
		if((++fuenteOcupada)>=2){
			
			pthread_cond_signal(&condicionFuente);
			/*if(finalizar == 1){
				char matar[100];
				sprintf(matar, "Me he quedado en la fuente colgado sin beber y me han matado");
				writeLogMessage(numAtleta,matar);
				printf("aquiaqui\n");
				pthread_cancel(atletas[atletaID].hiloAtleta);
			}*
			
		}
		pthread_cond_wait(&condicionFuente, &semaforoFuente);
		char estaBebiendo[100];
		sprintf(estaBebiendo, "El atleta está bebiendo ya porque le ayudan");
		char atleta[100];
		sprintf(atleta, "atleta_%d ",atletaID);
		// liberamos de la cola
		atletas[posAtleta].ha_competido = 2;
		atletas[posAtleta].idAtleta = 0;
		atletas[posAtleta].necesita_beber = 0;
		writeLogMessage(atleta,estaBebiendo);
		pthread_mutex_unlock(&semaforoFuente);
		
	}
	*/
}

//void irAFuente()



int calculaAleatorios(int min, int max) {
	int num = rand() % (max-min+1) + min;
	return num;
	
}

/* Calculamos la posición del atleta en la función sacarPosicion() */
int sacarPosicion(int identificadorAtleta) {
	int i=0, posicion;
	while(i<10){
		if(atletas[i].idAtleta==identificadorAtleta){
			posicion=i;			
		}	
		i++;
	}
	return posicion;
}

//Cuando se recibe kill -2 se llama a esta función  que cambia la variable finalizar a 1 para terminar el programa
void finalizarPrograma(){
	if(signal(SIGINT, finalizarPrograma) == SIG_ERR){
		perror("Error al recibir la senal para finalizar la competición");
		exit(-1);
	}

	char c[20];
	sprintf(c, "COMPETICION");
	char f[100];
	sprintf(f, "Pitido final de la competición, no accederán más atletas.");
	writeLogMessage(c,f);
	
	// cambiamos el flag de finalizado
	// no recibimos más señales de nuevos competidores
	finalizar=1;
	
	while(noHayNadieEnCola() == -1){
		// si entra aquí quiere decir que hay alguien todavía
		sleep(1);
	}
	
	/*finalizar = 2;

	int i;
	for(i = 0; i<numeroTarimas; i++){
		pthread_join(hiloTarima[i], NULL);
	}
	/*char oroID[100];
	sprintf(oroID, "atleta_%d ",campeones[0].idAtleta);
	char oroPts[100];
	sprintf(oroPts, "Medalla de Oro - %d pts.",campeones[0].puntos );
	writeLogMessage(oroID, oroPts);

	char plataID[100];
	sprintf(plataID, "atleta_%d ",campeones[1].idAtleta);
	char plataPts[100];
	sprintf(plataPts, "Medalla de Plata - %d pts.",campeones[1].puntos);
	writeLogMessage(plataID, plataPts);

	char bronceID[100];
	sprintf(bronceID, "atleta_%d ",campeones[2].idAtleta);
	char broncePts[100];
	sprintf(broncePts, "Medalla de Bronce - %d pts.",campeones[2].puntos);
	writeLogMessage(bronceID, broncePts);

	char campeonato[100];
	char cierre[100];
	sprintf(campeonato, "El campeonato de powerlifting");
	sprintf(cierre, "Termina hoy.");
	writeLogMessage(campeonato, cierre);
	*/
	
}

// devuelve la posicion de medalla si es mejor, para actualizar, y -1 si no
int buscaMejorPosicion(int puntos){
	int x;
	for(x = 0; x<3 ; ++x){
		if(campeones[x].puntos <= puntos){
			return x;
		}
	}
	return -1;
}

// actualiza el podio según los nuevos puntos que vayan obteniendo los atletas
void actualizaPodio(int id, int puntos){
	int mejor = buscaMejorPosicion(puntos);
	if(mejor != -1){
		int i = 0;
		for( i = 2; i>mejor;--i){
			int puntosTemp = campeones[i-1].puntos;
			int idTemp = campeones[i-1].idAtleta;
			campeones[i].puntos = puntosTemp;
			campeones[i].idAtleta = idTemp;
		}
		campeones[mejor].puntos = puntos;
		campeones[mejor].idAtleta = id;
	}
}

void mostrarEstadisticas(){
	// preparamos la señal otra vez por si a caso
	if(signal(SIGPIPE, mostrarEstadisticas) == SIG_ERR){
		perror("Error al recibir la señal para mostrar las estadísticas.");
		exit(-1);
	}
	// creamos contadores para stats
	// inicializamos a 0 por si no hubiera ningún atendido/esperando
	int contadorAtendiendo = 0, contadorEsperando = 0;
	
	// tenemos que mostrar estadisticas tanto en consola como en log 
	printf("\nESTADÍSTICAS:\n");
	char stat[20];
	sprintf(stat, "  STATS  ");

	// mostrar el número de atletas que se están atendiendo
	int i;
	pthread_mutex_lock(&semaforoColaAtletas);
	for(i = 0; i<numeroAtletas; i++){
		if(atletas[i].ha_competido == 1){
			contadorAtendiendo++;
		}
	}
	pthread_mutex_unlock(&semaforoColaAtletas);

	printf("\tNúmero de atletas compitiendo en pista: %d\n", contadorAtendiendo);
	char atend[100];
	sprintf(atend, "Número de atletas compitiendo--> %d", contadorAtendiendo);
	writeLogMessage(stat, atend);
	
	// mostrar el número de atletas que están esperando
	pthread_mutex_lock(&semaforoColaAtletas);
	for(i = 0; i<numeroAtletas; i++){
		if(atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0){
			contadorEsperando++;
		}
	}
	pthread_mutex_unlock(&semaforoColaAtletas);

	printf("\tNúmero de atletas esperando en cola: %d\n", contadorEsperando);
	char esperando[100];
	sprintf(esperando, "Número de atletas esperando--> %d", contadorEsperando);
	writeLogMessage(stat, esperando);


	// mostrar el número de atletas totales pasados por el sistema
	printf("\tNúmero de atletas que han accedido al sistema de la competición: %d\n", contadorAtletas);
	char nTot[50];
	sprintf(nTot, "Número de Atletas Totales--> %d", contadorAtletas);
	writeLogMessage(stat, nTot);
}
	

void writeLogMessage( char *id , char *msg){

	pthread_mutex_lock(&semaforoFicheroLog);
	
	// Calculamos la hora actual
	time_t now = time (0) ;
	struct tm *tlocal = localtime(&now);
	char stnow [19];
	strftime(stnow, 19, "%d/%m/%y %H:%M:%S", tlocal);

	// Escribimos en el log
	fichero = fopen(NOMBRE_FICHERO, "a");
	fprintf(fichero, "[%s] %s: %s \n" , stnow, id, msg);
	fclose(fichero);

	pthread_mutex_unlock(&semaforoFicheroLog);
}


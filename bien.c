#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#define RED "\e[0;31m"
#define YELLOW "\e[0;33m"
#define ENDCOLOR "\e[0m"

/************************************************************
// 
************************************************************/

#define ATLETAS_PREDEF 10
#define TARIMAS_PREDEF 2
int numeroAtletas;
int numeroTarimas;

pthread_mutex_t semaforoColaAtletas;
pthread_mutex_t semaforoFicheroLog;
pthread_mutex_t semaforoPodio;
pthread_mutex_t semaforoFin;
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

/* hilos pertenecientes a los atletas que vayan a la fuente */
pthread_t *hiloFuente;

// flag finalizar toma los valores de:
//	0: esperando en cola
//	1: se cierran las puertas de la competicion (SIGINT)
//	2: no hay ningún atleta que esté esperando para competir ni compitiendo
//	3: quedaba uno en fuente y le matamos
int finalizar;

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
void *fuente(void *posAtleta);
int buscaMejorPosicion(int puntos);
void actualizaPodio(int id, int puntos);
void mostrarEstadisticas();
void noHayNadieYa();
int quedanCompitiendo();
int quedanEsperando();


int main(int argc, char *argv[]) {


	//srand(getpid()); /* semilla para el cálculo de aleatorios */


	// Comprobación de argumentos y asignación de número máximo de atletas y tarimas
	if(argc == 1){
		
		// si no pasamos argumentos al ejecutar, modelo inicial 10 y 2
		numeroAtletas = ATLETAS_PREDEF;
		atletas = (struct cola_Atletas*) malloc(sizeof(struct cola_Atletas) * numeroAtletas);
		numeroTarimas = TARIMAS_PREDEF;
		hiloFuente = (pthread_t *) malloc(sizeof(pthread_t) *numeroAtletas);
		
	
	} else if(argc == 3){

		// ejecutamos como ./programa atletas tarimas
		numeroAtletas = atoi(argv[1]);
		atletas = (struct cola_Atletas*) malloc(sizeof(struct cola_Atletas) * numeroAtletas);
		numeroTarimas = atoi(argv[2]);
		hiloFuente = (pthread_t *) malloc(sizeof(pthread_t) *numeroAtletas);

	} else {
		printf(RED "Argumentos erróneos.\t Sintaxis: ./ejecutable numeroAtletas numeroTarimas\n\n" ENDCOLOR);
		return -1;	
	}

	printf("\n\t********* Bienvenido a la competición *********\n");
	printf("El " RED "PID" ENDCOLOR " correspondiente a esta ejecución es:" RED " %d\n" ENDCOLOR, getpid());
	printf("\t- Introduce " YELLOW "kill -10 PID" ENDCOLOR " para mandar al atleta a la " YELLOW "tarima 1.\n" ENDCOLOR );
	printf("\t- Introduce " YELLOW "kill -12 PID" ENDCOLOR " para mandar al atleta a la " YELLOW "tarima 2.\n" ENDCOLOR );
	printf("\t- Introduce " YELLOW "kill -13 PID" ENDCOLOR " para mostrar " YELLOW "estadísticas" ENDCOLOR " de la competición.\n");
	printf("\t- Introduce " YELLOW "kill -2 PID para terminar" ENDCOLOR " la competición\n");
	printf("\t- La competición tendrá " YELLOW "%d atletas máximo" ENDCOLOR " en cola y " YELLOW "%d tarimas.\n" ENDCOLOR, numeroAtletas, numeroTarimas);
	
	
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
	if (pthread_mutex_init(&semaforoFin,NULL) != 0) exit(-1);
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
	for(i = 0; i<3; i++){
		/* Tenemos que inicializar cada uno de los componentes de struct de podio */
		// Inicializamos ids de atletas 
		campeones[i].idAtleta = 0;
		// Inicializamos puntos 	
		campeones[i].puntos = 0;
	}
	

	/* Inicializamos fichero log */
	/* w por si no existiera el fichero registroTiempos para crearlo */
	pthread_mutex_lock(&semaforoFicheroLog);
	fichero = fopen("registroTiempos.log", "w");
	fclose(fichero);
	pthread_mutex_unlock(&semaforoFicheroLog);

	char separador[50];
	sprintf(separador, "---------------------- ");

	/* guardar registro de comienzo de competicion en log */
	char info[100];
	char campeonato1[100];
	sprintf(info, "  INFO   ");
	sprintf(campeonato1, "La competición acaba de comenzar, pueden llegar ya atletas.");
	writeLogMessage(info, campeonato1);
	writeLogMessage(separador, separador);

	pthread_t hiloTarima[numeroTarimas];

	/* Creamos los 2 hilos de tarimas */
	for( i = 1; i<=numeroTarimas; i++){
		
		pthread_create(&hiloTarima[i-1], NULL, accionesTarima, (void *) &i);
		sleep(1);
	}

	/* Esperamos señal sigusr1 o sigusr2 */
	/* bucle infinito para esperar una de ellas */
	while(finalizar==0){//mientras finalizar sea 0 bucle infinito para esperar las señales
		pause();	/* con pause en el momento que recibimos una vamos a la manejadora */
	}



	// con este join esperaria a que acaben las tarimas en caso de que pille a un juez descansando
	/*int x;
	for(x = 0; x < numeroTarimas; x++){
		pthread_join(hiloTarima[x], NULL);
	}*/
	
	writeLogMessage(separador, separador);
	
	// mostramos PODIO
	char podio[10];
	sprintf(podio, " PODIO ");
	writeLogMessage(info, podio);

	char podioNoOcupado[15];
	sprintf(podioNoOcupado, "-------- ");
	char oroID[100];
	sprintf(oroID, "atleta_%d ",campeones[0].idAtleta);
	char oroPts[100];
	sprintf(oroPts, "Medalla de Oro - %d pts.",campeones[0].puntos );
	if(campeones[0].idAtleta == 0){
		writeLogMessage(podioNoOcupado, oroPts);
	} else {
		writeLogMessage(oroID, oroPts);
	}
	

	char plataID[100];
	sprintf(plataID, "atleta_%d ",campeones[1].idAtleta);
	char plataPts[100];
	sprintf(plataPts, "Medalla de Plata - %d pts.",campeones[1].puntos);
	if(campeones[1].idAtleta == 0){
		writeLogMessage(podioNoOcupado, plataPts);
	} else {
		writeLogMessage(plataID, plataPts);
	}
	

	char bronceID[100];
	sprintf(bronceID, "atleta_%d ",campeones[2].idAtleta);
	char broncePts[100];
	sprintf(broncePts, "Medalla de Bronce - %d pts.",campeones[2].puntos);
	if(campeones[2].idAtleta == 0){
		writeLogMessage(podioNoOcupado, broncePts);
	} else {
		writeLogMessage(bronceID, broncePts);
	}
	

	writeLogMessage(separador, separador);
	
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

	char colaLlena[100];
	char info[20];
	sprintf(colaLlena, "La cola está llena, no pueden acceder más atletas ahora.");
	sprintf(info, "  INFO   ");


	int sitio;
	pthread_mutex_lock(&semaforoColaAtletas);
	sitio = haySitioEnCola();
	pthread_mutex_unlock(&semaforoColaAtletas);
	if(sitio != -1){
		pthread_mutex_lock(&semaforoColaAtletas);
		contadorAtletas++;
		atletas[sitio].idAtleta = contadorAtletas;
		//pthread_mutex_unlock(&semaforoColaAtletas);
		if(sig==SIGUSR1){
			//pthread_mutex_lock(&semaforoColaAtletas);
			atletas[sitio].tarima_asignada = 1;
			//pthread_mutex_unlock(&semaforoColaAtletas);
			
				//si recibimos SIGUSR2:
		} else if(sig==SIGUSR2){
			//pthread_mutex_lock(&semaforoColaAtletas);
			atletas[sitio].tarima_asignada = 2;
			//pthread_mutex_unlock(&semaforoColaAtletas);
		}
		pthread_mutex_unlock(&semaforoColaAtletas);

		pthread_create(&atletas[sitio].hiloAtleta, NULL, accionesAtleta,(void *) &atletas[sitio].idAtleta);
	
	} else{
		// si la función haySitioEnCola devuelve un -1 quiere decir que no hay y que está llena la cola
		writeLogMessage(info, colaLlena);
	}

}

// método que busca una posición libre de la cola si la hay
int haySitioEnCola(){
	int x;
	for(x = 0; x < numeroAtletas; x++){
		if(atletas[x].idAtleta == 0){
			return x; //Encuentra posición libre
		}
	}
	return -1; //No hay sitio libre
}



// metodo que comprueba y espera a que no haya nadie en la competicion
void noHayNadieYa(){
	int hay = -1;

	while(hay != 0){
		hay = -1;

		int i;
		pthread_mutex_lock(&semaforoColaAtletas);
		for(i = 0; i< numeroAtletas; i++){
			if(atletas[i].idAtleta != 0){
				hay = 1;
			}
		}

		pthread_mutex_unlock(&semaforoColaAtletas);

		if (hay == -1){
			hay = 0;
		}


		if(hay == 1){
			sleep(2);
		}
	}

	pthread_mutex_lock(&semaforoFin);
	finalizar = 2;
	pthread_mutex_unlock(&semaforoFin);

	pthread_mutex_lock(&semaforoFuente);
	pthread_cond_signal(&condicionFuente);
	pthread_mutex_unlock(&semaforoFuente);


}



/********************************************************************************/
/***************************** ACCIONES ATLETA **********************************/
/********************************************************************************/

void *accionesAtleta (void *idAtleta ) { /*Acciones de los atletas en el circuito */
	
	/* Pasamos a entero el puntero *idAtleta */
	int identificadorAtleta = *(int*)idAtleta;
	int posicion = sacarPosicion(identificadorAtleta);

	
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

	/* Comprueba si tiene que beber agua o tiene algun problema los atletas que estén en la cola cada 3 segundos */
	while(atletas[posicion].ha_competido == 0) {
		
		if(atletas[posicion].ha_competido == 0){	
			/* calculamos el aleatorio de la probabilidad del 15% de irse por problemas*/
				/* 1 si SI, 0 si NO */
			int alea = aleatorioSalud(15, identificadorAtleta);

			if(alea == 1){
				/* atleta tiene problemas de deshidratación y electrolitos y abandona cola */
				/* Abandona la cola de la tarima y se escribe en el log */
				char nosubeTarima[100];
				sprintf(nosubeTarima, "No llega a subir a la tarima por problemas de salud.");
				writeLogMessage(numeroAtleta, nosubeTarima);

				pthread_mutex_lock(&semaforoColaAtletas);
				/* Se libera espacio en la cola */
				atletas[posicion].necesita_beber = 0;
				atletas[posicion].idAtleta = 0;
				atletas[posicion].tarima_asignada = 0;
				atletas[posicion].ha_competido = 0;
				pthread_mutex_unlock(&semaforoColaAtletas);
				/* Se da fin al hilo */
				pthread_exit(NULL);	
				
			}else if (alea == 0) {	/* Si no se tienen problemas se esperan 3 segs para volver a comprobar */
				sleep(3);
			}
		}	
	}

	
	// Cuando el atleta haya competido podemos avanzar, le esperamos
	while(atletas[posicion].ha_competido==1) {
		// aquí el atleta está compitiendo
		// esperamos a que cambie el flag
	}
	
	// el atleta ha competido ya
	
	// liberamos espacio en la cola del que se va
	pthread_mutex_lock(&semaforoColaAtletas);
	atletas[posicion].idAtleta = 0;
	atletas[posicion].necesita_beber = 0;
	atletas[posicion].tarima_asignada = 0;
	atletas[posicion].ha_competido = 0;
	pthread_mutex_unlock(&semaforoColaAtletas);

	/* Se da fin al hilo */
	pthread_exit(NULL);
					
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
	sprintf(nTarima, "tarima_%d ", identificadorTarima);
	char ayudaTarima[100];

	char pasaron[20];
	
	int atletaElegido;
	/* Decimos que la tarima está libre */
	int ocupada = 0;
	
	/* buscamos un atleta, el que más tiempo lleve esperando, prioridad: su tarima */
	while(finalizar != 3){
		
		int i;
		int posAtleta = 0, variable = 999;		// esta variable nos sirve para identificar quien ha estado esperando más en la cola
		// Mientras la tarima no esté ocupada 
		while((ocupada == 0) && (finalizar != 3)){

			// bloqueamos semaforo, solo puede entrar uno a la vez a tarima 
			pthread_mutex_lock(&semaforoColaAtletas);
			int cont1 = 0, cont2 = 0;
			// buscamos atletas
			for (i = 0; i < numeroAtletas; i++){
				// miramos primero que sea de esta tarima
				if(atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0 && atletas[i].tarima_asignada == identificadorTarima){
					
					if(atletas[i].idAtleta < variable){
						cont1++;
						atletaElegido = atletas[i].idAtleta;
						if(cont1>=2){
							atletas[posAtleta].ha_competido = 0;
						}
						posAtleta = i;
						atletas[posAtleta].ha_competido = 1;
						// el atleta entra a tarima 
						ocupada = 1;
						variable = atletas[i].idAtleta;
						pthread_mutex_unlock(&semaforoColaAtletas);	// este era el original
					}
				}
			}
			
			if(ocupada == 0){
				
				// ahora buscamos de otra tarima que no sea la prioritaria 
				for (i = 0; i < numeroAtletas; i++){
					// miramos primero que sea de esta tarima
					if(atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0){
						
						if(atletas[i].idAtleta < variable){
							sprintf(ayudaTarima, "Entra el atleta_%d de la otra cola para avanzar.",atletas[i].idAtleta);
							writeLogMessage(nTarima, ayudaTarima);
							cont2++;	
							atletaElegido = atletas[i].idAtleta;
							if(cont2>=2){
								atletas[posAtleta].ha_competido = 0;
							}
							posAtleta = i;
							atletas[posAtleta].ha_competido = 1;
							ocupada = 1;
							variable = atletas[i].idAtleta;
							// el atleta entra a tarima
							pthread_mutex_unlock(&semaforoColaAtletas);
						}
					}
				}
			}
			// en el caso de que no llegue ningun atleta a ninguna cola tb tenemos que unlock
			pthread_mutex_unlock(&semaforoColaAtletas);
			
			if(ocupada == 0){
				sleep(1);
			}

		}

		if(ocupada == 1){

			// realizamos el levantamiento 
			levantamiento(atletaElegido, posAtleta);
			
			// ha realizado el levantamiento en sí, espera a que el juez le diga si tiene que beber
			char beber[100];
			sprintf(beber, "El juez le manda beber agua.");
			char atleta[100];
			sprintf(atleta, "atleta_%d ",atletaElegido);
			// El juez le manda ir a beber agua independientemente del caso con 10% de probabilidad
			// 1 si tiene que beber, 0 si no
			int aBeber = aleatorioSalud(10, atletaElegido);

			if(aBeber == 1){
				// al entrar en la fuente la tarima ya no estaría ocupada
				ocupada = 0;
				int p;
				
				p = posAtleta;

				pthread_create(&hiloFuente[posAtleta], NULL, fuente,(void *) &p);
		
				writeLogMessage(atleta, beber);
				
			} else {
				
				char noBeber[100];
				sprintf(noBeber, "El juez le dice que no necesita beber y se va a su casa ya.");
				writeLogMessage(atleta, noBeber);
				
				pthread_mutex_lock(&semaforoColaAtletas);	
				atletas[posAtleta].ha_competido = 2;
				pthread_mutex_unlock(&semaforoColaAtletas);
				if(quedanCompitiendo() == 0 && quedanEsperando() == 0){
					while(finalizar != 2){
					sleep(1);
					}
					pthread_mutex_lock(&semaforoFin);
					finalizar = 3;
					pthread_mutex_unlock(&semaforoFin);
				}
				

			}
				

			// incrementamos el contador de atletas que han competido
			competidos++;
		

			char juez[100];
			sprintf(juez, "tarima_%d ", identificadorTarima);
			char descansa1[100];
			sprintf(descansa1,"El juez empieza su descanso.");
			char descansa2[100];
			sprintf(descansa2, "El juez vuelve a la tarima.");
			char nosVamos[100];
			sprintf(nosVamos, "El juez estaba en descanso pero no es necesario más porque nos vamos todos ya.");

			// cada 4 descanso de jueces
			if(competidos % 4 == 0){
				/*ocupada = 1;
				writeLogMessage(juez, descansa1);
				sleep(10);
				writeLogMessage(juez, descansa2);*/
				ocupada = 1;
				writeLogMessage(juez, descansa1);
				int cuenta10 = 0;
				while(cuenta10 < 10){
					sleep(1);
					cuenta10++;
					if(finalizar != 0 && finalizar != 1){
						cuenta10 = 11;
						writeLogMessage(juez, nosVamos);
					}
				}
				if(cuenta10 == 10){
					writeLogMessage(juez, descansa2);
				}
			}

			ocupada = 0;
		}
		
		
		// aqui volvemos a buscar... bucle infinito
	}

	// se ha dado a finalizar
	//char pasaron[20];
	sprintf(pasaron, "Pasaron %d atletas.", competidos);
	writeLogMessage(nTarima, pasaron);	

	pthread_exit(NULL);
}

int quedanCompitiendo(){
	int i;
	for(i = 0; i<numeroAtletas; i++){
		if(atletas[i].ha_competido == 1){
			return 1;
		}
	}
	return 0;
}

int quedanEsperando(){
	int i;
	for(i = 0; i<numeroAtletas; i++){
		if(atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0){
			return 1;
		}
	}
	return 0;
}

void *fuente(void *posAtleta){

	int posicion = *(int*) posAtleta;
	int id, haCompe, needBeber, tarima;
	id = atletas[posicion].idAtleta;
	
	pthread_mutex_lock(&semaforoColaAtletas);
	atletas[posicion].idAtleta = 0;
	atletas[posicion].ha_competido = 2;
	atletas[posicion].necesita_beber = 1;
	pthread_mutex_unlock(&semaforoColaAtletas);

	char estaBebiendo[100];
	char estaEsperando[100];
	char ayuda[100];
	char atleta[15];
	sprintf(atleta, "atleta_%d ", id);

	pthread_mutex_lock(&semaforoFuente);
	if(fuenteOcupada == 1){
		// mandamos la señal - ayudamos a otro atleta si estuviera
		sprintf(ayuda, "Ayuda a beber en la fuente.");
		writeLogMessage(atleta, ayuda);
		pthread_cond_signal(&condicionFuente);		
	}
	sprintf(estaEsperando, "Está esperando a que alguien le ayude a beber.");
	
	writeLogMessage(atleta, estaEsperando);
	fuenteOcupada = 1;
	pthread_cond_wait(&condicionFuente, &semaforoFuente);
	if(finalizar == 2){
		// una vez que ya no hay nadie pendiente por competir miramos si queda alguien en fuente
		char muerte[100];
		sprintf(muerte, "Estaba esperando para beber pero me voy porque me dejaron colgado.");
		writeLogMessage(atleta, muerte);
		// cambiamos para esperar a que echemos al que quedaba
		pthread_mutex_lock(&semaforoFin);
		finalizar = 3;
		pthread_mutex_unlock(&semaforoFin);

	} else {
		// llegó señal de ayudante
		sprintf(estaBebiendo, "El atleta está bebiendo ya porque le ayudan.");
		writeLogMessage(atleta, estaBebiendo);
	}
	pthread_mutex_unlock(&semaforoFuente);

	pthread_exit(NULL);
}



/********************************************************************************/
/*************************** FUNCIONES AUXILIARES *******************************/
/********************************************************************************/

void levantamiento(int atletaID, int posAtleta){

	int opc, aBeber, tiempo, puntos;
	opc = aleatorioLevantamiento(80, 10, 10);
	char numAtleta[100];
	sprintf(numAtleta, "atleta_%d ", atletaID);
	char inicioLevantamiento[100];
	sprintf(inicioLevantamiento, "Comienza a realizar el levantamiento.");
	
	writeLogMessage(numAtleta, inicioLevantamiento);

	char motivoFin[100];

	switch(opc){
		case 1:	/* movimiento válido */
			
			puntos = calculaAleatorios(60,300);
			sprintf(motivoFin, "Resultado: Movimiento valido - puntuacion: %d.", puntos);
			tiempo = calculaAleatorios(2,6);
			sleep(tiempo);
			break;
		case 2:	/* nulo por normas */
			puntos = 0;
			sprintf(motivoFin, "Resultado: Movimiento nulo por normas - puntuacion: %d.", puntos);
			tiempo = calculaAleatorios(1,4);
			sleep(tiempo);
			break;
		case 3:	/* nulo por fuerzas */
			puntos = 0;
			sprintf(motivoFin, "Resultado: Movimiento nulo por fuerzas - puntuacion: %d.", puntos);
			tiempo = calculaAleatorios(6,10);
			sleep(tiempo);
			break; 
	}
	char finLevantamiento[100];
	sprintf(finLevantamiento, "Fin del levantamiento, ha tardado %d segundos.", tiempo);
	writeLogMessage(numAtleta, finLevantamiento);
	writeLogMessage(numAtleta, motivoFin);
	
	// asignamos los puntos en el podio
	pthread_mutex_lock(&semaforoPodio);
	actualizaPodio(atletaID, puntos);	
	pthread_mutex_unlock(&semaforoPodio);	

}

// Cuando se recibe kill -2 se llama a esta función  que cambia la variable finalizar a 1 para terminar el programa
void finalizarPrograma(){
	if(signal(SIGINT, finalizarPrograma) == SIG_ERR){
		perror("Error al recibir la senal para finalizar la competición");
		exit(-1);
	}

	char separador[50];
	sprintf(separador, "---------------------- ");
	writeLogMessage(separador, separador);

	char i[20];
	sprintf(i, "  INFO   ");
	char f[100];
	sprintf(f, "PITIDO FINAL de la competición, no accederán más atletas.");
	writeLogMessage(i,f);

	writeLogMessage(separador, separador);
	
	// cambiamos el flag de finalizado
	// no recibimos más señales de nuevos competidores
	pthread_mutex_lock(&semaforoFin);
	finalizar = 1;
	pthread_mutex_unlock(&semaforoFin);
	

	noHayNadieYa();
	

	while(finalizar != 3){
		sleep(1);
	}

	// se va al main
}

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

// muestra numero de atletas compitiendo, esperando y totales de la compe en el momento
void mostrarEstadisticas(){
	// preparamos la señal otra vez por si a caso
	if(signal(SIGPIPE, mostrarEstadisticas) == SIG_ERR){
		perror("Error al recibir la señal para mostrar las estadísticas.");
		exit(-1);
	}
	
	char separador[50];
	sprintf(separador, "---------------------- ");
	writeLogMessage(separador, separador);

	// creamos contadores para stats
	// inicializamos a 0 por si no hubiera ningún atendido/esperando
	int contadorAtendiendo = 0, contadorEsperando = 0;
	
	// tenemos que mostrar estadisticas tanto en consola como en log 
	char stat[20];
	sprintf(stat, "  STATS  ");

	// mostrar el número de atletas que se están atendiendo
	int i;
	for(i = 0; i<numeroAtletas; i++){
		if(atletas[i].ha_competido == 1){
			contadorAtendiendo++;
		}
	}
	char atend[100];
	sprintf(atend, "Número de atletas compitiendo--> %d", contadorAtendiendo);
	writeLogMessage(stat, atend);
	
	// mostrar el número de atletas que están esperando
	for(i = 0; i<numeroAtletas; i++){
		if(atletas[i].idAtleta != 0 && atletas[i].ha_competido == 0){
			contadorEsperando++;
		}
	}
	
	char esperando[100];
	sprintf(esperando, "Número de atletas esperando--> %d", contadorEsperando);
	writeLogMessage(stat, esperando);


	// mostrar el número de atletas totales pasados por el sistema
	
	char nTot[50];
	sprintf(nTot, "Número de Atletas Totales--> %d", contadorAtletas);
	writeLogMessage(stat, nTot);

	writeLogMessage(separador, separador);
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "context.h"
#include "lbeTransform.h"
#include "teclado.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

float leftAccel, rightAccel;
int iter1, iter2;

//MAC Estas variables son alteradas desde la clase teclado
extern float rAngle, rSpeed, rSign;
extern int exit_condition;

unsigned* keystate;

//Aquí vamos a guardar ambos shaders una vez compilados: le hacemos un attach a ambos a este objeto.
GLuint programObject;

static int init_gl(void)
{
	return 0;
}

static void draw(uint32_t i)
{	
	glClearColor (0.3f, 0.3f, 0.2f, 1.0f);	
	glClear(GL_COLOR_BUFFER_BIT);

	glDrawArrays(GL_TRIANGLES, 0, 3);
	eglSwapBuffers(eglInfo.display, eglInfo.surface);
	//Sólo para el contexto DRM/KMS
	DRM_PageFlip();	
}

//Recuerda: los uniform son las variables que le pasamos desde el programa en C, 
//Los attributes se llaman así porque son atributos del vértice, que es lo que se dedica a procesar el shader,
//un vértice por cada vez que se ejecuta su código.
//Las variables gl_* ya vienen definidas y son los valores que le pasamos de vuelta a GLES.
//Así, gl_Position es la posición final de un vértice (tras las transformaciones de cámara/modelo/proyección)
//y gl_fragColor es el color del píxel de cada fragment.
//Este vertex shader hace lo mínimo que debe hacer: dar la posición en eye coordinates (clipping space) del vértice.
//Recuerda también que las entradas (attributes, uniforms) se pueden llamar como te de la gana. Sólo tienes que
//tener en cuenta que las salidas tienen nombres fijos: gl_Position en el vertex shader y gl_FragColor en el fragment.
//Recuerda además que un vértice tiene tres atributos: posición, color y normal.
//Recuerda además que para pasar información de un shader a otro se usan los varying.

GLbyte vertexShaderSrc[] = 
	"uniform mat4 modelviewprojection;	\n"
	"attribute vec4 vertexPosition;		\n"
	"attribute vec4 vertexColor;		\n"
	"varying vec4 vyVertexColor;		\n"
	"void main () {				\n"
//	"	vyVertexColor = vec4 (1.0, 0.0, 0.0, 1.0);		\n"
	"	vyVertexColor = vertexColor;				\n"
	"	gl_Position = modelviewprojection * vertexPosition;	\n"
	"}					\n"
;

GLbyte fragmentShaderSrc[] = 
	"precision mediump float;			 \n"
	"varying vec4 vyVertexColor;			 \n"
	"void main (){				 	 \n"
	"	 gl_FragColor = vyVertexColor;		 \n"
	//"	 gl_FragColor = vec4 (0.0, 1.0, 0.0, 1.0);	 \n"
	"}					 	 \n"
;

GLuint CompileShader (GLuint shaderType, const char *shaderSrc){
	//Función que recibe el tipo de un shader (un define de GL que indica si es un GL_VERTEX_SHADER o 
	//un GL_FRAGMENT_SHADER) y el texto del programa del shader, y se encarga de crear el objeto shader,
	//compilar el programa del shader y comprobar si se ha compilado bien. 
	//Devuelve el objeto compilado como un GLuint.  
	GLuint shader;
	GLuint hasCompiled;

	shader = glCreateShader(shaderType);	
	
	if (shader == 0) return -1;
	
	//Cargamos los fuentes el shader
	glShaderSource (shader, 1, &shaderSrc, NULL);
	//Y los compilamos
	glCompileShader (shader);
	//Comprobamos que ha compilado correctamente, recuperando del objeto shader un array (vect) de enteros (iv)
	glGetShaderiv (shader, GL_COMPILE_STATUS, &hasCompiled);
	
	if (!hasCompiled){
		//Si no ha compilado correctamente, recuperamos otro dato del objeto shader que es una cadena 
		//con la causa del fallo.
		GLuint infoLen = 0;
		glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &infoLen);	
		char* infoLog = malloc (infoLen * sizeof(char));
		glGetShaderInfoLog (shader, infoLen, NULL, infoLog);
		printf ("Error de compilación de shader: %s\n", infoLog);
			
		return -1;
	}
	return shader;
} 

int setupShaders (){
	//Esta función carga los fuentes de los shaders, los manda a compilar, los coloca en el programObject
	//Recuerda: Compilar shaders, attacharlos al program object, linkar el program object
	GLuint vertexShader;
	GLuint fragmentShader;
	GLint isLinked;

	//Mandamos a compilar los fuentes de los shaders
	vertexShader = CompileShader (GL_VERTEX_SHADER, vertexShaderSrc);
	fragmentShader = CompileShader (GL_FRAGMENT_SHADER, fragmentShaderSrc);
	
	programObject = glCreateProgram();

	if (programObject == 0 ) {
		printf ("Error: no se pudo crear program object");
		return -1;
	}
	
	glAttachShader (programObject, vertexShader);
	glAttachShader (programObject, fragmentShader);

	glLinkProgram (programObject);

	glGetProgramiv (programObject, GL_LINK_STATUS, &isLinked);		
	
	//Si no ha linkado, recuperamos el mensaje de error del objeto programa
	if (!isLinked){
		char *infoLog;
		GLint infoLen = 0;
		glGetProgramiv (programObject, GL_INFO_LOG_LENGTH, &infoLen);
		infoLog = malloc (infoLen * sizeof(char));
		glGetProgramInfoLog (programObject, infoLen, NULL, infoLog);
		printf ("Error linkando objeto programa de los shaders: %s\n", infoLog);	
		return -1;
	}	
	
	glUseProgram (programObject);

	return 0;
}

float vertices[] = {0.0f, 0.5f, 0.0f,
		   -0.5f, -0.5f, 0.0f,
		    0.5f, -0.5f, 0.0f};

int main(int argc, char *argv[])
{
	int ret = init_drm();
	if (ret) {
		printf("failed to initialize DRM\n");
		return ret;
	}

	ret = init_gbm();
	if (ret) {
		printf("failed to initialize GBM\n");
		return ret;
	}

	ret = init_egl();
	if (ret) {
		printf("failed to initialize EGL\n");
		return ret;
	}

	ret = init_gl();
	if (ret) {
		printf("failed to initialize GLES\n");
		return ret;
	}

	setupShaders();


	//Establecemos el viewport
	printf ("Usando modo de vídeo %d x %d\n", eglInfo.width, eglInfo.height);
	glViewport (0, 0, eglInfo.width, eglInfo.height);
	
	//MAC Bloque de paso de geometría a memoria de GLES

	//Triángulo con la misma longitud de base (1 unidad) que de altura
	//Dados en órden de RHS, situado en el plano Z=0
	float vertices[] = {0.0f, 0.5f, 0.0f,
			   -0.3f, -0.5f, 0.0f,
			    0.3f, -0.5f, 0.0f};
	
	float colors[] = {
			    1.0, 0.0, 0.0,		//Rojo
			    0.0, 1.0, 0.0,		//Verde
			    0.0, 0.0, 1.1		//Azul	
	};	
	
	//Creamos el buffer object para poder colocar el array de vértices en un lugar de memoria accesible para GLES
	//Se usa para guardar todos los atributos de los vértices: posición, color, normal.
	GLuint vertexBuffer;
	glGenBuffers (1, &vertexBuffer);
	
	//Colocamos el array en memoria de GLES: lo bindamos al GL_ARRAY_BUFFER,y a través de eso le pasamos los dats
	//El buffer object donde vamos a guardar los vértices va a tener, además de su posición, su color asociado y
	//si nos interesa incluso la normal de cada uno. Para ello, primero se inicializa el buffer con la llamada
	//a glBufferData(), y luego ya se meten los datos con glBufferSubData()
	//Originalmente, como sólo había que subir el array de las posiciones, se hacía directamente en glBufferData()
	//glBindBuffer (GL_ARRAY_BUFFER, vertexBuffer);
	//glBufferData (GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer (GL_ARRAY_BUFFER, vertexBuffer);

	//Calculamos los offsets dentro del buffer object de cada uno de los atributos: posición, color, normal
	//Usamos uintptr_t porque son enteros que nos garantizan que guardan un puntero sin problemas.
	//Acostúmbrate a la conversión entre entero sin signo y puntero, porque un puntero es un entero sin signo.
	uintptr_t positionsOffset = 0;
	uintptr_t colorsOffset = (sizeof(vertices));
	uintptr_t normalsOffset = (sizeof(vertices)) + sizeof(colors);

	glBufferData (GL_ARRAY_BUFFER, sizeof(vertices) + sizeof(colors), NULL, GL_STATIC_DRAW);
	glBufferSubData (GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	glBufferSubData (GL_ARRAY_BUFFER, colorsOffset, sizeof(colors), colors);
	
	//Cogemos el número de atributo de vertexPosition en el shader. Alternativamente, podríamos especificar
	//el nuestro con glBindAttribLocation(), pero antes de linkar el programa.
	GLint attPositions;
	GLint attColors;
	attPositions = glGetAttribLocation (programObject, "vertexPosition"); 
	attColors =    glGetAttribLocation (programObject, "vertexColor");
	//Activamos cada atributo para que pueda ser usado desde el vertex shader
	glEnableVertexAttribArray(attPositions);	
	glEnableVertexAttribArray(attColors);	

	//Aquí lo que hacemos es pasar las direccions de los datos de los atributos: posición, color, normal. 
	//Esos atributos se pasan al buffer object que esté bound al GL_ARRAY_BUFFER en este momento, tras
	//la última llamada a glBindBuffer()
	//RECUERDA: UN BUFFER OBJECT, TRES ATRIBUTOS DE CADA VÉRTICE QUE GUARDAMOS EN ÉL
	
	glVertexAttribPointer(attPositions, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)(positionsOffset));
	glVertexAttribPointer(attColors, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)colorsOffset);

	//Deshacemos el binding para no alterar los datos del buffer sin querer.
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//Subimos a GL la matriz de transformación + proyeccción. La secuencia es parecida.
	//Pendiente agrupar todas las variables que son operacionales a este nivel en una estructura.
	lbeMatrix mvp;
	lbeLoadIdentity (&mvp);	
	GLint mvpOBJ;
	mvpOBJ = glGetUniformLocation (programObject, "modelviewprojection");	
	glUniformMatrix4fv(mvpOBJ, 1, GL_FALSE, &mvp.m[0][0]);

	
	lbePrintMatrix (&mvp);
	//Vamos a animar un poco las cosas, a base de ir actualizando la matriz modelviewprojection
	int loops = 0;
	int exit_condition = 0;
	while (!exit_condition) {
		lbeRotate (&mvp, 0, 1, 0, 1.5f);
		//lbePrintMatrix (&mvp);
		glUniformMatrix4fv(mvpOBJ, 1, GL_FALSE, &mvp.m[0][0]);
		draw (0);	
		if (loops >= 500)
			exit_condition = 1;
		loops++;
	}
	return 0;
}
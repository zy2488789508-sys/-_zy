#ifndef RELAY1_H
#define RELAY1_H

#include "stm32f10x.h"
#include <stdbool.h>
typedef struct
{

	_Bool Relay1_Status;

} RELAY1_INFO;

typedef struct
{

	_Bool Relay2_Status;

} RELAY2_INFO;

typedef struct
{

	_Bool Relay3_Status;

} RELAY3_INFO;



#define RELAY1_ON		1
#define RELAY1_OFF	0

#define RELAY2_ON		1
#define RELAY2_OFF	0

#define RELAY3_ON		1
#define RELAY3_OFF	0



extern RELAY1_INFO relay1_info;
extern RELAY2_INFO relay2_info;
extern RELAY3_INFO relay3_info;


void Relay_init(void);

void Relay1_Set(_Bool status);
void Relay2_Set(_Bool status);
void Relay3_Set(_Bool status);

#endif



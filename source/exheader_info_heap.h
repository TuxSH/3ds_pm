#pragma once

#include <3ds/exheader.h>

void ExHeaderInfoHeap_Init(void *buf, size_t num);
ExHeader_Info *ExHeaderInfoHeap_New(void);
void ExHeaderInfoHeap_Delete(ExHeader_Info *data);

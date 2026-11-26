#pragma once

/*
 * Every RTOS task this firmware creates is spawned from one file so stack size,
 * priority and core are auditable in a single place, and the boot audit prints
 * exactly what runs.
 */
void app_tasks_start(void);

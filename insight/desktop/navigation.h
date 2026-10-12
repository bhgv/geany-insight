/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_NAVIGATION_H
#define INSIGHT_NAVIGATION_H
#include <geanyplugin.h>
#include "index.h"
typedef struct Navigation Navigation;
Navigation *navigation_new(GeanyPlugin *);
void navigation_free(Navigation *);
const InsightIndex *navigation_index(Navigation *);
#endif

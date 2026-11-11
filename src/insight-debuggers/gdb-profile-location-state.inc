/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Represent stable C++ source selectors separately from process-local GDB location numbers.
 * RU: Отделяет устойчивые координаты исходников C++ от номеров подпозиций конкретного сеанса GDB. */

typedef struct
{
    gchar *file, *function;
    guint line;
} NativeSavedLocation;

typedef struct
{
    guint point_index;
    gchar *parent_number;
    NativeSavedLocation *location;
    gboolean applied;
} NativeLocationOverride;

typedef struct
{
    gchar *number;
    guint matches;
    gboolean enabled;
} NativeLocationCandidate;

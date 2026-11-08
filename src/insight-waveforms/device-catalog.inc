/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Device presets, search and type filters.
 * RU: Настройки устройств, поиск и фильтры типов.
 */

typedef enum
{
    DEVICE_SETUP,
    DEVICE_TIMING,
    DEVICE_INPUT,
    DEVICE_INDICATOR,
    DEVICE_SCOPE,
    DEVICE_UART,
    DEVICE_MEMORY,
    DEVICE_SPI,
    DEVICE_I2C,
    DEVICE_CONSTANT,
    DEVICE_HEX
} DeviceKind;

static const gchar *const device_kind_names[] = {"Setup",
                                                 "Clock and timing",
                                                 "Input controls",
                                                 "Indicators",
                                                 "Oscilloscope and plots",
                                                 "UART",
                                                 "Memory",
                                                 "SPI",
                                                 "I²C",
                                                 "Constant signals",
                                                 "Hex tables"};

static const gchar *const device_kind_icons[] = {"preferences-system-symbolic",
                                                 "appointment-soon-symbolic",
                                                 "input-gaming-symbolic",
                                                 "display-brightness-symbolic",
                                                 "utilities-system-monitor-symbolic",
                                                 "utilities-terminal-symbolic",
                                                 "drive-harddisk-symbolic",
                                                 "network-wired-symbolic",
                                                 "network-transmit-receive-symbolic",
                                                 "insert-text-symbolic",
                                                 "view-grid-symbolic"};

typedef struct
{
    const gchar *rule, *fields, *pins;
    gint image;
    gboolean script;
    const gchar *title;
    DeviceKind icon;
    guint categories;
} DevicePreset;

static const DevicePreset device_presets[] = {
    {"module", "Module name=", "", -1, FALSE, "Design module", DEVICE_SETUP, (1u << DEVICE_SETUP)},
    {"clock", "Port=clk|Start (ns)=0|High (ns)=5|Low (ns)=5", "i1", -1, FALSE, "Clock", DEVICE_TIMING,
     (1u << DEVICE_TIMING)},
    {"pulse", "Port=reset|Start (ns)=10|Length (ns)=100|Active level=1", "i1", -1, FALSE, "Timed pulse",
     DEVICE_TIMING, (1u << DEVICE_TIMING)},
    {"button", "Port=button|Idle level=0|Length (ns)=100", "i1", -1, FALSE, "Push button", DEVICE_INPUT,
     (1u << DEVICE_INPUT)},
    {"switch", "Port=enable|Initial level=0", "i1", -1, FALSE, "Switch", DEVICE_INPUT,
     (1u << DEVICE_INPUT)},
    {"slider", "Port=level|Width=8|Initial value=0", "i@1", -1, FALSE, "Slider", DEVICE_INPUT,
     (1u << DEVICE_INPUT)},
    {"led", "Port=ready|Active level=1", "o1", -1, FALSE, "LED", DEVICE_INDICATOR,
     (1u << DEVICE_INDICATOR)},
    {"display", "Port=status|Width=8|Format (hex, dec, bin)=hex", "o@1", -1, FALSE,
     "Numeric / hex display", DEVICE_INDICATOR, (1u << DEVICE_INDICATOR)},
    {"scope", "Signal=", "a64", -1, FALSE, "Oscilloscope", DEVICE_SCOPE, (1u << DEVICE_SCOPE)},
    {"scope-xy", "X signal=|Y signal=", "a64|a64", -1, FALSE, "XY plot", DEVICE_SCOPE,
     (1u << DEVICE_SCOPE)},
    {"scope-xyz", "X signal=|Y signal=|Z signal=", "a64|a64|a64", -1, FALSE, "XYZ plot", DEVICE_SCOPE,
     (1u << DEVICE_SCOPE)},
    {"usart", "RX=rx|TX=tx|XCK=xck|Sample edge=rise|Console=console", "i1|o1|o1", -1, FALSE,
     "USART (DUT clock)", DEVICE_UART, (1u << DEVICE_UART)},
    {"usart-clock", "RX=rx|TX=tx|XCK=xck|Clock period ns=20|Sample edge=rise|Console=console",
     "i1|o1|i1", -1, FALSE, "USART (testbench clock)", DEVICE_UART, (1u << DEVICE_UART)},
    {"uart", "RX=rx|TX=tx|Baud=115200|Console=console", "i1|o1", -1, FALSE, "UART console", DEVICE_UART,
     (1u << DEVICE_UART)},
    {"parallel-psram",
     "Address=addr|Data=dq|CE#=ce_n|WE#=we_n|OE#=oe_n|LB#=lb_n|UB#=ub_n|Address bits=20",
     "o@7|b16|o1|o1|o1|o1|o1", 0, FALSE, "Parallel PSRAM x16", DEVICE_MEMORY,
     (1u << DEVICE_MEMORY)},
    {"sram",
     "Address=addr|Data=dq|CE#=ce_n|"
     "WE#=we_n|OE#=oe_n|Address bits=16|Data bits=8",
     "o@5|b@6|o1|o1|o1", 0, FALSE, "SRAM", DEVICE_MEMORY, (1u << DEVICE_MEMORY)},
    {"sdram",
     "CK=ck|CKE=cke|CS#=cs_n|RAS#=ras_n|"
     "CAS#=cas_n|WE#=we_n|A=a|BA=ba|DQ=dq|DQM=dqm",
     "o1|o1|o1|o1|o1|o1|o13|o2|b16|o2", 6, FALSE, "SDR SDRAM", DEVICE_MEMORY, (1u << DEVICE_MEMORY)},
    {"ddr1",
     "CK=ck|CKE=cke|CS#=cs_n|RAS#=ras_n|CAS#=cas_n|WE#=we_n|"
     "A=a|BA=ba|DQ=dq|DQS=dqs|DM=dm|Read latency=2|Write latency=1",
     "o1|o1|o1|o1|o1|o1|o13|o2|b8|b1|o1", 6, FALSE,
     "DDR1", DEVICE_MEMORY, (1u << DEVICE_MEMORY)},
    {"ddr2",
     "CK=ck|CKE=cke|CS#=cs_n|RAS#=ras_n|CAS#=cas_n|WE#=we_n|"
     "A=a|BA=ba|DQ=dq|DQS=dqs|DM=dm|Read latency=3|Write latency=2",
     "o1|o1|o1|o1|o1|o1|o14|o3|b8|b1|o1", 6, FALSE, "DDR2", DEVICE_MEMORY, (1u << DEVICE_MEMORY)},
    {"ddr3",
     "CK=ck|RESET#=reset_n|CKE=cke|CS#=cs_n|RAS#=ras_n|CAS#=cas_n|"
     "WE#=we_n|A=a|BA=ba|DQ=dq|DQS=dqs|DM=dm|Read latency=3|Write latency=2",
     "o1|o1|o1|o1|o1|o1|o1|o16|o3|b8|b1|o1", 7, FALSE, "DDR3", DEVICE_MEMORY, (1u << DEVICE_MEMORY)},
    {"ddr4",
     "CK=ck|RESET#=reset_n|CKE=cke|CS#=cs_n|ACT#=act_n|A=a|BA=ba|"
     "BG=bg|DQ=dq|DQS=dqs|DM#=dm_n|Read latency=3|Write latency=2",
     "o1|o1|o1|o1|o1|o17|o2|o2|b8|b1|o1", 5, FALSE, "DDR4", DEVICE_MEMORY, (1u << DEVICE_MEMORY)},
    {"ddr5",
     "CK=ck|RESET#=reset_n|CS#=cs_n|CA=ca|DQ=dq|DQS=dqs|DM#=dm_n|Read latency=3|Write latency=2",
     "o1|o1|o1|o14|b8|b1|o1", 3, FALSE, "DDR5", DEVICE_MEMORY, (1u << DEVICE_MEMORY)},
    {"spi-nor", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", 1, FALSE, "SPI NOR", DEVICE_SPI,
     (1u << DEVICE_SPI) | (1u << DEVICE_MEMORY)},
    {"spi-nand", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", 1, FALSE, "SPI NAND", DEVICE_SPI,
     (1u << DEVICE_SPI) | (1u << DEVICE_MEMORY)},
    {"spi-eeprom", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", 1, FALSE, "SPI EEPROM",
     DEVICE_SPI, (1u << DEVICE_SPI) | (1u << DEVICE_MEMORY)},
    {"spi-psram", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", 1, FALSE, "SPI PSRAM",
     DEVICE_SPI, (1u << DEVICE_SPI) | (1u << DEVICE_MEMORY)},
    {"i2c-eeprom", "SCL=scl|SDA=sda|Address=0x50", "c1|b1", 1, FALSE, "I²C EEPROM", DEVICE_I2C,
     (1u << DEVICE_I2C) | (1u << DEVICE_MEMORY)},
    {"spi-controller",
     "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso|TX hex=9f000000|Mode=0|Phase ns=50|Start ns=100",
     "i1|i1|i1|o1", -1, FALSE, "SPI Controller", DEVICE_SPI, (1u << DEVICE_SPI)},
    {"i2c-controller", "SCL=scl|SDA=sda|Address=0x50|TX hex=00|Read bytes=1|Phase ns=2500|Start ns=100",
     "d1|b1", -1, FALSE, "I²C Controller", DEVICE_I2C, (1u << DEVICE_I2C)},
    {"spi-lua", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", -1, TRUE, "Lua SPI", DEVICE_SPI,
     (1u << DEVICE_SPI)},
    {"i2c-lua", "SCL=scl|SDA=sda|Address=0x50", "c1|b1", -1, TRUE, "Lua I²C", DEVICE_I2C,
     (1u << DEVICE_I2C)},
    {"hex-table", "Port=status|Width=32", "o@1", -1, FALSE, "Hex byte table", DEVICE_HEX,
     (1u << DEVICE_HEX) | (1u << DEVICE_INDICATOR)},
    {"value", "Port=data|Width=8|Value=0", "i@1", -1, FALSE, "Constant signal", DEVICE_CONSTANT,
     1u << DEVICE_CONSTANT},
};

enum
{
    DEVICE_ID,
    DEVICE_ICON,
    DEVICE_NAME,
    DEVICE_INDEX,
    DEVICE_COLUMNS
};

typedef struct
{
    GtkWidget *combo, *search, *regex, *category;
    GtkListStore *store;
    gint preferred;
    gulong selection_changed;
} DeviceCatalog;

/* EN: Return the selected preset index from combo, or -1 if no device matches.
 * RU: Возвращает индекс выбранного шаблона из combo или -1, если список пуст. */
static gint device_catalog_selected(GtkComboBox *combo)
{
    GtkTreeIter row;
    if (!gtk_combo_box_get_active_iter(combo, &row))
        return -1;
    gint index;
    gtk_tree_model_get(gtk_combo_box_get_model(combo), &row, DEVICE_INDEX, &index, -1);
    return index;
}

/* EN: Refilter the DeviceCatalog in data after widget changes. Preserve a matching selection and flag an invalid regex.
 * RU: Фильтрует DeviceCatalog из data при изменении widget. Сохраняет подходящий выбор и отмечает неверное регулярное выражение. */
static void device_catalog_filter(GtkWidget *widget, gpointer data)
{
    (void)widget;
    DeviceCatalog *catalog = data;
    GtkComboBox *combo = GTK_COMBO_BOX(catalog->combo);
    gint previous = device_catalog_selected(combo);
    gint selected = previous >= 0 ? previous : catalog->preferred;
    if (previous >= 0)
        catalog->preferred = previous;
    gint category = gtk_combo_box_get_active(GTK_COMBO_BOX(catalog->category)) - 1;
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(catalog->search));
    gboolean regex = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(catalog->regex));
    GError *error = NULL;
    GRegex *pattern = regex && *query ? g_regex_new(query, G_REGEX_CASELESS, 0, &error) : NULL;
    gchar *folded = g_utf8_casefold(query, -1);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(catalog->search), GTK_ENTRY_ICON_PRIMARY,
                                      error ? "dialog-error-symbolic" : "edit-find-symbolic");
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(catalog->search), GTK_ENTRY_ICON_PRIMARY,
                                    error ? error->message : NULL);
    g_signal_handler_block(combo, catalog->selection_changed);
    gtk_list_store_clear(catalog->store);
    for (guint i = 0; !error && i < G_N_ELEMENTS(device_presets); i++)
    {
        const DevicePreset *preset = &device_presets[i];
        if (category >= 0 && !(preset->categories & (1u << category)))
            continue;
        gchar *label = g_strdup_printf("%s · %s", preset->title, preset->rule);
        gchar *lower = g_utf8_casefold(label, -1);
        gboolean matches = pattern ? g_regex_match(pattern, preset->rule, 0, NULL) ||
                                         g_regex_match(pattern, preset->title, 0, NULL)
                                   : strstr(lower, folded) != NULL;
        if (matches)
        {
            GtkTreeIter row;
            gtk_list_store_insert_with_values(catalog->store, &row, -1, DEVICE_ID, preset->rule,
                                              DEVICE_ICON, device_kind_icons[preset->icon], DEVICE_NAME,
                                              label, DEVICE_INDEX, (gint)i, -1);
        }
        g_free(lower);
        g_free(label);
    }
    if (selected < 0 || !gtk_combo_box_set_active_id(combo, device_presets[selected].rule))
        gtk_combo_box_set_active(combo, 0);
    g_signal_handler_unblock(combo, catalog->selection_changed);
    if (previous != device_catalog_selected(combo) || previous < 0)
        g_signal_emit_by_name(combo, "changed");
    g_clear_pointer(&pattern, g_regex_unref);
    g_clear_error(&error);
    g_free(folded);
}

/* EN: Build the device search controls and icon selector for catalog in box.
 * RU: Создаёт поиск устройств и список с пиктограммами для catalog в box. */
static void device_catalog_widget(DeviceCatalog *catalog, GtkWidget *box)
{
    GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    catalog->search = gtk_search_entry_new();
    gtk_widget_set_name(catalog->search, "insight-device-search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(catalog->search), "Find device");
    gtk_box_pack_start(GTK_BOX(filters), catalog->search, TRUE, TRUE, 0);
    catalog->regex = gtk_check_button_new_with_label("Device Regex");
    gtk_box_pack_start(GTK_BOX(filters), catalog->regex, FALSE, FALSE, 0);
    catalog->category = gtk_combo_box_text_new();
    gtk_widget_set_name(catalog->category, "insight-device-category");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(catalog->category), "All device types");
    for (guint i = 0; i < G_N_ELEMENTS(device_kind_names); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(catalog->category), device_kind_names[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(catalog->category), 0);
    gtk_box_pack_start(GTK_BOX(filters), catalog->category, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), filters, FALSE, FALSE, 4);

    catalog->store =
        gtk_list_store_new(DEVICE_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    catalog->combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(catalog->store));
    g_object_unref(catalog->store);
    gtk_widget_set_name(catalog->combo, "insight-test-device-type");
    gtk_combo_box_set_id_column(GTK_COMBO_BOX(catalog->combo), DEVICE_ID);
    GtkCellRenderer *icon = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(catalog->combo), icon, FALSE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(catalog->combo), icon, "icon-name", DEVICE_ICON);
    GtkCellRenderer *label = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(catalog->combo), label, TRUE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(catalog->combo), label, "text", DEVICE_NAME);
    gtk_box_pack_start(GTK_BOX(box), catalog->combo, FALSE, FALSE, 6);
    g_signal_connect(catalog->search, "changed", G_CALLBACK(device_catalog_filter), catalog);
    g_signal_connect(catalog->regex, "toggled", G_CALLBACK(device_catalog_filter), catalog);
    g_signal_connect(catalog->category, "changed", G_CALLBACK(device_catalog_filter), catalog);
}

typedef struct
{
    GtkWidget *grid, *file, *reset, *status, *active, *dialog;
    GPtrArray *entries;
    PortBrowser *browser;
    const gchar *directory;
} DeviceForm;

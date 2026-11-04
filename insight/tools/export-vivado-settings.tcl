# SPDX-License-Identifier: GPL-2.0-or-later
# Source in the Vivado Tcl Console, then call geany_export_hdl_settings.
proc geany_export_hdl_settings {output} {
    set fileset [current_fileset]
    if {$fileset eq ""} { error "Open a Vivado project first" }
    set base [get_property DIRECTORY [current_project]]
    set records [list geany-insight-vivado-v1]
    foreach directory [get_property INCLUDE_DIRS $fileset] {
        lappend records "include\t[file normalize [file join $base $directory]]"
    }
    foreach define [get_property VERILOG_DEFINE $fileset] {
        lappend records "define\t$define"
    }
    set top [get_property TOP $fileset]
    if {$top ne ""} { lappend records "top\t$top" }
    foreach record $records {
        if {[regexp {[\r\n]} $record]} { error "Setting contains a line break" }
    }
    set out [open $output w]
    try {
        fconfigure $out -encoding utf-8 -translation lf
        puts $out [join $records "\n"]
    } finally {
        close $out
    }
    return [file normalize $output]
}

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

# Export a new project alongside its slang configuration. Do not overwrite a
# Geany project that may already contain manually maintained membership.
proc geany_export_project {output} {
    set output [file normalize $output]
    if {[file extension $output] ne ".geany"} { error "Expected a .geany filename" }
    set config "[file rootname $output].hdl.tsv"
    if {[file exists $output] || [file exists $config]} { error "Choose a new export filename" }
    set project [current_project]
    set fileset [current_fileset]
    if {$project eq "" || $fileset eq ""} { error "Open a Vivado project first" }
    set base [get_property DIRECTORY $project]
    set files [dict create]
    foreach object [concat [get_files -compile_order sources -used_in synthesis -of_objects $fileset] \
                           [get_files -of_objects $fileset]] {
        set path [file normalize [file join $base [get_property NAME $object]]]
        if {[string tolower [file extension $path]] ni {.v .sv .vh .svh .vhd .vhdl}} { continue }
        if {[file isfile $path]} { dict set files $path 1 }
    }
    if {![dict size $files]} { error "No HDL sources found in the current Vivado fileset" }
    set escape [list "\\" "\\\\" "\n" "\\n" "\r" "\\r" "\t" "\\t" ";" "\\;" " " "\\s"]
    set encoded [list]
    foreach path [dict keys $files] { lappend encoded [string map $escape $path] }
    geany_export_hdl_settings $config
    set out [open $output {WRONLY CREAT EXCL}]
    try {
        fconfigure $out -encoding utf-8 -translation lf
        puts $out {[project]}
        puts $out "name=[string map $escape [get_property NAME $project]]"
        puts $out "base_path=[string map $escape [file normalize $base]]"
        puts $out {file_patterns=}
        puts $out {[insight-project-files]}
        puts $out {version=1}
        puts $out {automatic=false}
        puts $out {exclude-masks=}
        puts $out "files=[join $encoded {;}];"
        foreach key {trees excluded-files excluded-trees} { puts $out "$key=" }
    } finally {
        close $out
    }
    return [list $output $config]
}

#/bin/sh
#\
exec wish $0

wm iconify .

proc createTerm {sock} {
    global socket
    toplevel .$sock
    text .$sock.t -yscrollcommand ".$sock.v set" 
    scrollbar .$sock.v -command ".$sock.t yview"
    .$sock.t tag configure output -foreground red
    .$sock.t tag configure input -foreground darkgreen
    grid rowconfigure .$sock 0 -weight 1
    grid columnconfigure .$sock 0 -weight 1
    grid .$sock.t .$sock.v -sticky nsew
    bind .$sock.t <Destroy> "close $sock"
    bind .$sock.t <F1> "%W delete 0.1 end"
    set socket(.$sock.t) $sock
    focus .$sock.t
}

socket -server connect 40000

proc connect {sock addr port} {
    fconfigure $sock -blocking 0 -buffering none
    createTerm $sock
    fileevent $sock readable "receiveHandler $sock"
}

proc receiveHandler {sock} {
    set a [read $sock]
    if [eof $sock] {
        destroy .$sock
        return
    }
    .$sock.t mark set insert end
    .$sock.t insert end $a output
    .$sock.t see end
    if {[string range $a 0 4] == "echo "} {
        .$sock.t insert end [string range $a 5 end]
        .$sock.t see end
        puts -nonewline $sock [string range $a 5 end]
    }
}

if {[info proc tkTextInsert] != ""} {
    set insert tkTextInsert
    set paste tkTextPaste
} else {
    set insert tk::TextInsert
    set paste tk_textPaste
}

rename $insert tkTextInsert_org
rename $paste tkTextPaste_org

proc $insert {w s} {
    global socket
    if {[string equal $s ""] || [string equal [$w cget -state] "disabled"]} {
        return
    }
    $w mark set insert end
    $w insert end $s
    $w see end
    puts -nonewline $socket($w) $s
}

proc $paste {w x y} {
    global insert
    set s [selection get -displayof $w]
    $insert $w $s
}

#remove bindings on Control-<letter>
for {set ascii 0x61} {$ascii <= 0x7a} {incr ascii} {
    bind Text <Control-[format %c $ascii]> ""
}
#remove bindings on symbolic tags
foreach tag {Clear Paste Copy Cut} {
    bind Text <<$tag>> ""
}

bind Text <Control-Key> [list $insert %W %A]

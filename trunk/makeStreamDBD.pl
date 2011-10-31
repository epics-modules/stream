use strict;
use warnings;

my @srcs = @ARGV[2..$#ARGV];
my $dbd;
my $out;
# stream.dbd
if ($ARGV[0] == 1) {
    open($out, ">",  "$ARGV[1]") or die "Can't open $ARGV[1]\n";
    foreach $dbd (@srcs) {
        print $out "device(".$dbd.",INST_IO,dev".$dbd."Stream,\"stream\")\n";
    }
    print $out "driver(stream)\n";
    print $out "variable(streamDebug, int)\n";
    print $out "registrar(streamRegistrar)\n";  
    close $out;
}
# streamSynApps.dbd
elsif ($ARGV[0] == 2) {
    open($out, ">",  "$ARGV[1]") or die "Can't open $ARGV[1]\n";
    foreach $dbd (@srcs) {
        print $out "device(".$dbd.",INST_IO,dev".$dbd."Stream,\"stream\")\n";
    }
    close $out;
}
# streamReferences for BUSSES
elsif ($ARGV[0] == 3) {
    open($out, ">",  "$ARGV[1]") or die "Can't open $ARGV[1]\n";
    foreach $dbd (@srcs) {
        print $out "extern void* ref_".$dbd."Interface;\n";
        print $out "void* p".$dbd." = ref_".$dbd."Interface;\n"; 
    }
    close $out;
}
# streamReferences for FORMAT
elsif ($ARGV[0] == 4) {
    open($out, ">>",  "$ARGV[1]") or die "Can't open $ARGV[1]\n";
    foreach $dbd (@srcs) {
        print $out "extern void* ref_".$dbd."Converter;\n";
        print $out "void* p".$dbd." = ref_".$dbd."Converter;\n"; 
    }
    close $out;
}
# stream.dbd for 3-13
elsif ($ARGV[0] == 5) {
    open($out, ">",  "$ARGV[1]") or die "Can't open $ARGV[1]\n";
    foreach $dbd (@srcs) {
        print $out "device(".$dbd.",INST_IO,dev".$dbd."Stream,\"stream\")\n";
    }
    print $out "driver(stream)\n";
    close $out;
}
else {print "\n\nCannot Generate DBD Files\n\n";}


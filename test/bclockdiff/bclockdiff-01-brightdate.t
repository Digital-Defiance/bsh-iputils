#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# BrightDate test for bclockdiff

use Test::Command tests => 2;
use Test::More;

my $lib = File::Basename::dirname(Cwd::abs_path($0)) . '/../lib.pl';
require "$lib";

my $bclockdiff = get_cmd($ARGV[0] // 'bclockdiff');

# --brightdate basic output
{
    my $cmd = Test::Command->new(cmd => "$bclockdiff --brightdate --unit=μd 127.0.0.1");
    $cmd->exit_is_num(0);
    subtest 'BrightDate output' => sub {
        $cmd->stdout_like(qr/BrightDate: rtt=[0-9.eE+-]+μd delta=[0-9.eE+-]+μd/, 'Prints BrightDate units');
    }
}

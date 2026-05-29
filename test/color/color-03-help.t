#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Every b* tool documents shared color options in --help output.

use Test::Command;
use Test::More;
use File::Basename;
use Cwd;

my $here = File::Basename::dirname(Cwd::abs_path($0));
require "$here/../lib.pl";

my @tools = qw(bping btraceroute bmtr baudit bclockdiff);

for my $tool_name (@tools) {
    my $tool = get_cmd($tool_name);
    subtest "$tool_name --help documents color options" => sub {
        my $cmd = Test::Command->new(cmd => "$tool --help");
        if ($tool_name eq 'bclockdiff') {
            ok($cmd->{exit} == 0 || $cmd->{exit} == 2, '--help exit status');
        } else {
            $cmd->exit_is_num(2);
        }
        $cmd->stderr_like(qr/--color/);
        $cmd->stderr_like(qr/--no-color/);
        $cmd->stderr_like(qr/--color-scheme/);
    };
}

done_testing;

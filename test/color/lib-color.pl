#!/usr/bin/perl -w
# SPDX-License-Identifier: GPL-2.0-or-later
# Shared helpers for iputils_color integration tests.

use strict;
use warnings;

our @EXPORT_OK = qw(
    color_probe_tool
    run_color_cmd
    stdout_has_ansi
    stdout_has_truecolor
);

use Exporter 'import';

my @COLOR_ENV_VARS = qw(
    NO_COLOR CLICOLOR CLICOLOR_FORCE COLORTERM
    IPUTILS_COLOR IPUTILS_COLOR_SCHEME
);

sub _with_color_env {
    my ($cb, %set) = @_;
    my %saved;
    for (@COLOR_ENV_VARS) {
        $saved{$_} = $ENV{$_} if exists $ENV{$_};
        delete $ENV{$_};
    }
    @ENV{keys %set} = values %set if %set;
    my $ret = $cb->();
    for (@COLOR_ENV_VARS) {
        if (exists $saved{$_}) {
            $ENV{$_} = $saved{$_};
        } else {
            delete $ENV{$_};
        }
    }
    return $ret;
}

sub run_color_cmd {
    my ($cmdline, %env) = @_;
    require Test::Command;
    return _with_color_env(sub {
        return Test::Command->new(cmd => $cmdline);
    }, %env);
}

# Default probe: bping to localhost with minimal BrightSpace args.
sub color_probe_tool {
    my ($tool, $extra) = @_;
    $extra //= '';
    return "$tool --my-coord=0,0,0 --target-coord=0,0,0 $extra 127.0.0.1";
}

sub stdout_has_ansi {
    my ($text) = @_;
    return defined($text) && $text =~ /\x1b\[/;
}

sub stdout_has_truecolor {
    my ($text) = @_;
    return defined($text) && $text =~ /38;2;/;
}

1;

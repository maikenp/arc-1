package LdifPrinter;

use MIME::Base64;

use LogUtils;
our $log = LogUtils->getLogger(__PACKAGE__);


sub new {
    my ($this, $handle) = @_; 
    my $class = ref($this) || $this;
    # This would only affect comment lines, the rest is guaranteed to be ASCII
    binmode $handle, ':encoding(utf8)'
        or $log->error("binmode failed: $!");
    #print $handle "# extended LDIF\n#\n# LDAPv3\n"
    #    or $log->error("print failed: $!");
    my $self = {fh => $handle, dn => undef, nick => undef, attrs => undef, disablecnt => 0};
    return bless $self, $class;
}

sub disableOut {
    my ($self) = @_;
    $self->{disablecnt} = $self->{disablecnt} + 1;
}

sub enableOut {
    my ($self) = @_;
    $self->{disablecnt} = $self->{disablecnt} - 1;
}

sub begin {
    my ($self, $dnkey, $name) = @_;
    $self->_flush() if defined $self->{dn};
    unshift @{$self->{dn}}, safe_dn("$dnkey=$name");
    unshift @{$self->{nick}}, safe_comment("$name");
}

sub attribute {
    my ($self, $attr, $value) = @_;
    if ($self->{disablecnt} == 0) {
        push @{$self->{attrs}}, [$attr, $value];
    }
}

sub attributes {
    my ($self, $data, $prefix, @keys) = @_;
    if ($self->{disablecnt} == 0) {
        my $attrs = $self->{attrs} ||= [];
        push @$attrs, ["$prefix$_", $data->{$_}] for @keys;
    }
}

sub end {
    my ($self) = @_;
    $self->_flush();
    shift @{$self->{dn}};
    shift @{$self->{nick}};
}

#
# Prints an entry with the attributes added so far.
# Prints nothing if there are no attributes.
#
sub _flush {
    my ($self) = @_;
    my $fh = $self->{fh};
    my $attrs = $self->{attrs};
    return unless defined $attrs;
    my $dn = join ",", @{$self->{dn}};
    my $nick = join ", ", @{$self->{nick}};
    print $fh "\n";
    #print $fh "# $nick\n";
    print $fh safe_attrval("dn", $dn)."\n"
        or $log->error("print failed: $!");
    for my $pair (@$attrs) {
        my ($attr, $val) = @$pair;
        next unless defined $val;
        if (not ref $val) {
            print $fh safe_attrval($attr, $val)."\n"
                or $log->error("print failed: $!");
        } elsif (ref $val eq 'ARRAY') {
            for (@$val) {
            print $fh safe_attrval($attr, $_)."\n"
                or $log->error("print failed: $!");
            }
        } else {
            $log->error("Not an ARRAY reference in: $attr");
        }
    }
    $self->{attrs} = undef;
}

#
# Make a string safe to use as a Relative Distinguished Name, cf. RFC 2253
#
sub safe_dn {
    my ($rdn) = @_;
    # Escape with \ the following characters ,;+"\<> Also escape # at the
    # beginning and space at the beginning and at the end of the string.
    $rdn =~ s/((?:^[#\s])|[,+"\\<>;]|(?:\s$))/\\$1/g;
    # Encode CR, LF and NUL characters (necessary except when the string
    # is further base64 encoded)
    $rdn =~ s/\x0D/\\0D/g;
    $rdn =~ s/\x0A/\\0A/g;
    $rdn =~ s/\x00/\\00/g;
    return $rdn;
}

#
# Construct an attribute-value string safe to use in LDIF, fc. RFC 2849
#
sub safe_attrval {
    my ($attr, $val) = @_;
    return "${attr}:: ".encode_base64($val,'') if $val =~ /^[\s,:<]/
                                               or $val =~ /[\x0D\x0A\x00]/
                                               or $val =~ /[^\x00-\x7F]/;
    return "${attr}: $val";
}

#
# Leave comments as they are, just encode CR, LF and NUL characters
#
sub safe_comment {
    my ($line) = @_;
    $line =~ s/\x0D/\\0D/g;
    $line =~ s/\x0A/\\0A/g;
    $line =~ s/\x00/\\00/g;
    return $line;
}

#
# Fold long lines and add a final newline. Handles comments specially.
#
sub fold78 {
    my ($tail) = @_;
    my $is_comment = "#" eq substr($tail, 0, 1);
    my $contchar = $is_comment ? "# " : " ";
    my $output = "";
    while (length $tail > 78) {
        $output .= substr($tail, 0, 78) . "\n";
        $tail = $contchar . substr($tail, 78);
    }
    return "$output$tail\n";
}


#
# Higher level functions for recursive printing
#
#   $collector  - a func ref that upon evaluation returns a hash ref ($data)
#   $idkey      - a key in %$data to be used to construct the relative DN component
#   $prefix     - to be prepended to the relative DN
#   $attributes - a func ref that is meant to print attributes. Called with $data as input.
#   $subtree    - yet another func ref that is meant to descend into the hierachy. Called
#                 with $data as input. Optional.
#

# Prints a single entry

sub Entry {
    my ($self, $collector, $prefix, $idkey, $attributes, $subtree) = @_;
    return unless $collector and my $data = &$collector();
    if ($data->{NOPUBLISH}) { $self->disableOut(); }
    $self->begin("$prefix$idkey", $data->{$idkey});
    &$attributes($self,$data);
    &$subtree($self, $data) if $subtree;
    $self->end();
    if ($data->{NOPUBLISH}) { $self->enableOut(); }
}

# Prints entries for as long as $collector continues to evaluate to non-null

sub Entries {
    my ($self, $collector, $prefix, $idkey, $attributes, $subtree) = @_;
    while ($collector and my $data = &$collector()) {
        if ($data->{NOPUBLISH}) { $self->disableOut(); }
        $self->begin("$prefix$idkey", $data->{$idkey});
        &$attributes($self,$data);
        &$subtree($self, $data) if $subtree;
        $self->end();
        if ($data->{NOPUBLISH}) { $self->enableOut(); }
    }
}



#### TEST ##### TEST ##### TEST ##### TEST ##### TEST ##### TEST ##### TEST ####

sub test {
    my $data;
    my $printer = LdifPrinter->new(*STDOUT);
    $printer->begin(o => "glue");
    $data = { objectClass => "organization", o => "glue" };
    $printer->attributes("", $data, qw(objectClass o));
    $printer->begin(GLUE2GroupID => "grid");
    $printer->attribute(objectClass => "GLUE2GroupID");
    $data = { GLUE2GroupID => "grid" };
    $printer->attributes("GLUE2", $data, qw( GroupID ));
    $printer->end();
    $printer->end();
} 

#test;

1;


#compdef tileroot
# zsh completion for tileroot — install to a directory in $fpath as _tileroot

_tileroot() {
    local -a commands
    commands=('dump:save the current layout' 'restore:restore a saved layout')

    if (( CURRENT == 2 )); then
        _describe 'command' commands
        return
    fi

    case ${words[2]} in
        dump)
            _arguments \
                '--workspace[scope to one workspace]:name:' \
                '-o[write to file]:file:_files' \
                '--pretty[human-readable box-drawing tree]' \
                '--verbose[diagnostic output]'
            ;;
        restore)
            _arguments \
                '--dry-run[show planned placement without doing it]' \
                '--verbose[diagnostic output]' \
                '1:session file:_files -g "*.json"'
            ;;
    esac
}

_tileroot

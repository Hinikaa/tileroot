# bash completion for tileroot — source this file, or install to
# /usr/share/bash-completion/completions/tileroot
_tileroot() {
    local cur prev words cword
    _init_completion || return

    local commands="dump restore"
    local dump_opts="--workspace -o --pretty --verbose"
    local restore_opts="--dry-run --verbose"

    if [[ $cword -eq 1 ]]; then
        COMPREPLY=($(compgen -W "$commands --version --help" -- "$cur"))
        return
    fi

    case "${words[1]}" in
        dump)
            case "$prev" in
                --workspace) return ;;  # workspace names aren't enumerable offline
                -o) _filedir; return ;;
            esac
            COMPREPLY=($(compgen -W "$dump_opts" -- "$cur"))
            ;;
        restore)
            case "$prev" in
                --dry-run|--verbose) ;;
                *) _filedir 'json' ;;
            esac
            COMPREPLY+=($(compgen -W "$restore_opts" -- "$cur"))
            ;;
    esac
}
complete -F _tileroot tileroot

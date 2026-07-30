# fish completion for tileroot — install to ~/.config/fish/completions/tileroot.fish

complete -c tileroot -f
complete -c tileroot -n __fish_use_subcommand -a dump -d 'save the current layout'
complete -c tileroot -n __fish_use_subcommand -a restore -d 'restore a saved layout'
complete -c tileroot -n __fish_use_subcommand -l version
complete -c tileroot -n __fish_use_subcommand -l help

complete -c tileroot -n '__fish_seen_subcommand_from dump' -l workspace -d 'scope to one workspace' -x
complete -c tileroot -n '__fish_seen_subcommand_from dump' -s o -d 'write to file' -r
complete -c tileroot -n '__fish_seen_subcommand_from dump' -l pretty -d 'human-readable box-drawing tree'
complete -c tileroot -n '__fish_seen_subcommand_from dump' -l verbose -d 'diagnostic output'

complete -c tileroot -n '__fish_seen_subcommand_from restore' -l dry-run -d 'show planned placement without doing it'
complete -c tileroot -n '__fish_seen_subcommand_from restore' -l verbose -d 'diagnostic output'
complete -c tileroot -n '__fish_seen_subcommand_from restore' -r -a '(__fish_complete_suffix .json)'

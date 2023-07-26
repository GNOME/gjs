sources = files(
  'testDecorators.ts',
)

tsc_config = files('tsconfig.json')

typescript = custom_target(
  'typescript-compile',
  input: sources,
  build_by_default: true,
  build_always_stale: true,
  depend_files: tsc_config,
  command: ['tsc', '-p', tsc_config, '--outDir', '@OUTDIR@'],
  output: ['testDecorators.js'],
  install: true,
  install_dir: installed_js_tests_dir / 'esnext'
)

esnext_tests = [
    'Decorators',
]

foreach test : esnext_tests

    # TODO: This just gets the first compiled file.
    test(test, minijasmine, args: [typescript[0], '-m'], env: tests_environment,
        protocol: 'tap', depends: typescript, suite: 'JS')

    esm_test_description_subst = {
        'name': 'test@0@.js'.format(test),
        'installed_tests_execdir': installed_tests_execdir,
    }
    esm_test_description = configure_file(
        configuration: esm_test_description_subst,
        input: '../../minijasmine-module.test.in',
        output: 'test@0@.test'.format(test),
        install: get_option('installed_tests'),
        install_dir: installed_tests_metadir)
endforeach
# GitHub — como colocar o projeto no repositório

Este pacote foi preparado para o fluxo:

GitHub → Actions → Windows Runner → CMake/OBS → ZIP do plugin

## Upload

No repositório `diretor-de-culto`, use **Add file → Upload files** e envie o conteúdo desta pasta.

É importante manter a pasta:

`.github/workflows/build.yml`

O GitHub Actions usa o template oficial do OBS durante o build para obter a infraestrutura de CMake/dependências. O projeto não depende de Lua.

## Depois do upload

1. Abra a aba **Actions**.
2. Entre em **Build Diretor de Culto**.
3. Clique no workflow.
4. Se necessário, use **Run workflow**.
5. Aguarde o Windows runner terminar.
6. Abra a execução concluída.
7. Em **Artifacts**, baixe:
   `Diretor-de-Culto-2.0.0-alpha-Windows-x64`

Dentro do ZIP estará o pacote gerado para o OBS.

## OBS 31.0.0

A configuração de build acompanha o baseline atual do template oficial (OBS 31.1.1). Isso é deliberado: não vou afirmar compatibilidade binária exata com 31.0.0 sem testar o DLL nesse runtime.

Se o seu OBS continuar em 31.0.0, primeiro faremos o build e teste. Se houver incompatibilidade, fixaremos o buildspec na revisão exata do OBS 31.0.0 antes de avançar.

## Importante

O plugin continua sendo 100% nativo:

C++ + Qt6 + OBS Frontend API.

O GitHub Actions é somente o ambiente que compila o DLL; ele não fica rodando junto com o OBS.

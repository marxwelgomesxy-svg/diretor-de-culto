# Diretor de Culto — Plugin nativo para OBS Studio

## Importante

Este projeto é **100% plugin nativo**.

Não utiliza:

- Lua
- Python
- script do OBS
- navegador externo
- servidor obrigatório
- API de IA externa

A primeira versão é um **alpha funcional do núcleo nativo**. Ela cria um Dock Qt dentro do OBS, lê a cena real, reage aos eventos do OBS e pode trocar cenas reais.

A inteligência de visão computacional e análise de áudio será adicionada como módulos C++ nativos na próxima etapa. Esta versão não finge detectar rosto, pessoa ou microfone quando ainda não existe esse motor.

## O que já existe

- Dock nativo "Diretor de Culto"
- C++17
- Qt6
- OBS Frontend API
- leitura da cena atual
- monitoramento de mudança de cena
- botão CORTAR
- botão IGNORAR
- Manual / Assistido / Automático
- intervalo mínimo de segurança entre cortes
- suporte a Studio Mode
- contador de cortes
- tempo de transmissão
- cena mais usada
- motor inicial baseado nos nomes das suas cenas
- tratamento das cenas EDIT/zoom:
  - `1 - PASTOR` <-> `PASTOR EDIT`
  - `2 - SOLO` <-> `SOLO EDIT`
  - `CAMERA 2` <-> `CAM EDIT 2`
- sequência HINO 1 -> HINO 2 ... HINO 15

## Compatibilidade

O projeto foi estruturado para o modelo atual de desenvolvimento de plugins do OBS, com CMake, `libobs`, `obs-frontend-api` e Qt6.

O OBS disponibiliza oficialmente `obs_frontend_add_dock_by_id()` para criar um Dock nativo dentro da interface e eventos como `OBS_FRONTEND_EVENT_SCENE_CHANGED` e `OBS_FRONTEND_EVENT_FINISHED_LOADING`.

Recomenda-se compilar usando o ambiente de desenvolvimento correspondente à versão do OBS instalada.

## Windows

Requisitos:

1. Visual Studio 2022 com Desktop development with C++
2. CMake 3.28+
3. Qt6 compatível com a versão do OBS
4. OBS Studio development libraries / OBS plugin development environment

O OBS oficial mantém um template de plugins com suporte a Visual Studio 17 2022, CMake e Qt6.

### Opção recomendada

Use o ambiente de desenvolvimento do template oficial do OBS para obter as dependências e configure este projeto com:

```text
ENABLE_FRONTEND_API=ON
ENABLE_QT=ON
```

Se as bibliotecas do OBS estiverem disponíveis pelo CMake:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release
```

O DLL será gerado em:

```text
build/windows-x64/RelWithDebInfo/obs-diretor-de-culto.dll
```

## Instalação manual do DLL

O plugin compilado deve ser instalado na pasta de plugins do OBS, normalmente dentro da instalação do OBS em:

```text
obs-plugins/64bit/
```

A estrutura exata pode variar conforme a instalação/distribuição.

## Próxima etapa do projeto

A arquitetura foi preparada para receber, ainda em C++:

### Motor de visão

- captura de frames das fontes/cenas
- OpenCV
- detecção de pessoa
- detecção de rosto
- posição/enquadramento
- movimento
- plano geral
- público
- altar
- pregador
- cantora
- músicos
- instrumentos

### Motor de áudio

- atividade de microfone
- intensidade
- voz
- música
- mudança de atividade
- associação áudio + cena

### Motor diretor

Entrada:

```text
VISÃO
+
ÁUDIO
+
CENA ATUAL
+
HISTÓRICO
+
TEMPO
+
REGRAS DO CULTO
```

Saída:

```text
CENA SUGERIDA
CONFIANÇA
MOTIVOS
AÇÃO
```

Depois:

```text
Manual
   ↓
Assistido
   ↓
Automático
```

## Observação

O alpha propositalmente não apresenta motivos falsos como "rosto detectado" ou "microfone ativo". Esses indicadores só devem aparecer quando o módulo nativo correspondente estiver realmente processando esses sinais.


## Build pelo GitHub

Este pacote agora inclui `.github/workflows/build.yml`. O workflow usa um Windows runner do GitHub e importa a infraestrutura oficial do template de plugins do OBS para configurar dependências, CMake e Qt6. O artefato final é publicado na execução do GitHub Actions para download. O projeto continua 100% C++/Qt, sem Lua.

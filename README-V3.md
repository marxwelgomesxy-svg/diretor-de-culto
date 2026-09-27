# Diretor de Culto V3 — OBS Studio 27.2.4

V3 mantém o plugin 100% nativo C++/Qt e acrescenta uma camada de IA local via Ollama, sem chave de API e sem cobrança por token.

## Recursos desta V3

- Interface corrigida e compilada explicitamente em UTF-8.
- Gestão de todas as cenas reais existentes no OBS.
- CORTAR e IGNORAR funcionais.
- Modos Manual, Assistido e Automático.
- Proteção mínima entre cortes.
- Monitoramento de nível de áudio do mix principal do OBS.
- Métrica local de movimento do vídeo.
- Captura de frame reduzido do programa para análise visual.
- Integração HTTP local com Ollama em `127.0.0.1:11434`.
- Prompt que obriga a IA a escolher somente uma cena existente.
- Sugestão com confiança e motivos.
- Se a IA estiver offline, o OBS continua funcionando.

## IA recomendada

Modelo multimodal leve: `qwen3-vl:2b`.

Instalação:

```powershell
ollama pull qwen3-vl:2b
```

Teste:

```powershell
ollama run qwen3-vl:2b
```

Ollama deve estar em execução para o Diretor consultar a IA local.

# Port experimental para PlayStation 2

O alvo e um ELF nativo para o Emotion Engine, usando a base Linux/SDL2.
Todas as unidades compartilhadas do jogo entram na compilacao. O estado atual
e **compilado, ainda sem validacao de boot ou jogabilidade no PCSX2/console**.
Os testes no host verificam o rasterizador e a conversao/decodificacao de video;
nao demonstram que a campanha funciona no PS2.

## Como testar no PCSX2

Use uma BIOS obtida do seu console e habilite **Host Filesystem** nas opcoes
de emulacao do PCSX2 (a localizacao depende da versao). Abra o ELF por
**Start File / Iniciar arquivo**. O acesso ao host permite ler os assets e
gravar os saves ao lado do programa.

Organize os arquivos assim; sao os dados PC/USA do jogo, nao uma imagem PS2:

```text
ResidentEvil/
  residentevil.elf
  config.ini
  USA/
    Data/
    Enemy/
    Stage1/
    ...
    Movie/
  SAVE/           # criado ao salvar
```

Copie `config.ps2.ini` como `config.ini` apenas se nao houver uma configuracao
existente. `[Assets] Path=.` significa que `USA/` esta ao lado do ELF. Se a
pasta instalada tem outro formato, use o migrador de assets descrito no README
ou ajuste `Path` para a pasta que contem `USA/`. `Version=JPN` seleciona `JPN/`.
O primeiro teste recomendado usa USA/OG sem packs HD ou overlays.

Sem o arquivo de fonte do jogo, o programa entra automaticamente numa tela de
diagnostico. Ela testa sleep/retomada/encadeamento de tarefas, apresenta cores
e animacao, envia um tom ao audio e mostra o estado do controle. CROSS testa o
botao e START sai. `--selftest`, quando passado como argumento ao ELF, executa
essa tela por 180 quadros mesmo com os assets presentes. O log do EE registra
`[PS2 TEST] scheduler sleep/chain/stack: PASS` ou `FAIL`. Audio e video tambem
precisam ser conferidos ouvindo o tom e observando a tela.

Depois, teste titulo, NEW GAME, movimento, inventario, uma troca de camera,
uma porta, som, FMV e salvar/carregar. Registre a versao do PCSX2 e os logs do
EE se houver erro. Esses testes dependem dos assets do usuario e ainda nao
foram executados neste ambiente.

## Videos

O backend PS2 usa MPEG1 + MP2 a 320x240/30 fps e 44100 Hz stereo, por streaming,
com uma fila PCM de 128 KiB. O decoder vendorizado e PL_MPEG (MIT), com revisao
registrada em `src/third_party/pl_mpeg.LICENSE.txt`.

No PC, com Python 3 e ffmpeg instalados:

```sh
python3 tools/prepare_ps2_movies.py /caminho/ResidentEvil/USA/Movie
```

O comando cria `nome.ps2.mpg` ao lado de cada AVI/MP4. Prefere MP4 quando
existem as duas versoes. Mantem os originais e os sidecars existentes;
`--overwrite` regenera os sidecars. Na build PS2, o seletor de filmes procura
essa extensao. Sem conversao, o filme nao pode ser reproduzido. Nao remova os
originais de uma arvore compartilhada com o port PC.

## Compilar

Requer uma instalacao oficial de [ps2dev](https://github.com/ps2dev/ps2dev),
incluindo PS2SDK, SDL2, gsKit e ps2_drivers, e CMake 3.16 ou posterior no host.
O Windows continua usando Game.vcxproj e o Linux o CMake padrao.

```sh
export PS2DEV=/caminho/ps2dev
export PS2SDK="$PS2DEV/ps2sdk"
export GSKIT="$PS2DEV/gsKit"
bash tools/build_ps2.sh
```

Saida: `build/ps2/residentevil.elf` com simbolos e `residentevil.map`.
`JOBS` controla o paralelismo (padrao 4); `CMAKE` permite indicar o executavel
CMake. No ambiente usado para desenvolver o port, PS2DEV esta em
`/workspace/ps2-deps/ps2dev`, e CMake em `/workspace/.re1-tools/bin/cmake`.
Foi utilizada a toolchain oficial com GCC 15.2.0 e SDL 2.32.10.

Para gerar um pacote sem assets, com uma copia do ELF sem informacoes de debug:

```sh
bash tools/package_ps2.sh
```

O ELF com simbolos permanece em `build/ps2` para usar com
`mips64r5900el-ps2-elf-addr2line` ao investigar enderecos do log.

## Validacao automatica

```sh
bash tests/check_ps2.sh
```

Executa, no host, testes com AddressSanitizer/UndefinedBehaviorSanitizer para
arestas compartilhadas, profundidade, perspectiva, filtros, recorte e blend;
converte um filme sintetico com ffmpeg, decodifica audio/video, busca e retoma;
e verifica a fronteira de APIs do jogo. Requer g++, Python e ffmpeg.

## Implementacao e limites

- Reutiliza diretamente `platform/linux/input.cpp` e `audio.cpp` com SDL2.
  O audio do PS2 usa 44100 Hz e uma fila limitada para FMV.
- O renderizador novo implementa MarniDX com rasterizacao na CPU e apresenta
  um framebuffer fixo de 320x240 usando SDL2/gsKit. Inclui z-buffer e correcao
  de perspectiva. **Nao ha medicao de FPS no PS2 ainda**; a rasterizacao na EE
  pode precisar ser substituida por primitivas GS/VU para desempenho jogavel.
- Texturas usam um limite de 8 MiB. Packs HD ficam desativados neste perfil,
  mesmo se habilitados no arquivo de configuracao. O tamanho maximo de uma
  textura e 1024x1024. Os buffers estaticos do jogo continuam intactos.
- O ELF ocupa aproximadamente 15 MiB de memoria estatica (codigo, dados e BSS),
  antes de pilhas, audio e texturas. Nao foi validado o pico de memoria com uma
  sala real. O PS2 tem 32 MiB; falta medir e ajustar com assets reais.
- Tarefas cooperativas usam threads EE e semaforos. Apenas uma tarefa do jogo
  executa por vez. Pilhas usam canarios verificados nas trocas; o PS2 nao tem
  as guard pages do Linux. O diagnostico exercita sleep e chain em runtime.
- Caminhos reconhecem dispositivos como `host:`, `mass:` e `mc0:`. O primeiro
  alvo de teste e PCSX2 + host filesystem. USB/memory card/hardware real ainda
  nao foram validados. IOP e drivers sao inicializados pelo SDL2main.
- Logs vao para stdout/stderr do EE. Nao ha traceback simbolizado automatico
  como no Linux; preserve o ELF com simbolos para diagnostico.

Nenhum asset comercial ou BIOS acompanha o codigo ou o pacote.

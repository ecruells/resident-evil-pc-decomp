# Downloads para PS2

- [Baixar pacote completo: ELF, config.ini, instruções e código-fonte](https://github.com/gogeta456/resident-evil-ps2/raw/refs/heads/PS2/downloads/resident-evil-ps2-experimental.zip)
- [Baixar somente residentevil.elf](https://github.com/gogeta456/resident-evil-ps2/raw/refs/heads/PS2/downloads/residentevil.elf)
- [Checksums SHA-256](SHA256SUMS)

O pacote completo é a opção recomendada. Coloque `residentevil.elf` e
`config.ini` na pasta que contém `USA/`. Preserve sua configuração existente
caso já tenha ajustado os caminhos. No PCSX2, habilite **Host Filesystem**
e abra o ELF usando **Start File / Iniciar arquivo**.

Esta é uma build **experimental**. A compilação e os testes no host passaram;
boot, desempenho e jogabilidade ainda precisam ser validados no PCSX2.
Sem os assets, o programa deve entrar na tela de diagnóstico. Os vídeos
precisam da conversão descrita nas [instruções do port](../docs/PS2_PORT.md).

Os arquivos não incluem assets comerciais nem BIOS.

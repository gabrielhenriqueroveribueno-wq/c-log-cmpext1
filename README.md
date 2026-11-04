# Controle de Entregas e Retiradas — CMPEXT1

Esboço minimamente funcional do projeto de extensão (TADS-EAD IFSP).
Demandante: empresa de locação de mesas e cadeiras.

## Como compilar e rodar

- **Linux / Mac:** `g++ -std=c++17 -o logistica main.cpp` e depois `./logistica`
- **Windows (Dev-C++, Code::Blocks, VS Code com MinGW):** abrir `main.cpp`, compilar e executar.
- **Visual Studio:** novo projeto "Aplicativo de Console" C++, colar o conteúdo de `main.cpp`.

Os arquivos `clientes.csv`, `servicos.csv` e `config.csv` são criados na mesma pasta do programa
na primeira gravação. Abrem no Excel (separador `;`).

## Mapa de funcionalidades

```
MENU PRINCIPAL
├── 1 Clientes ............ cadastrar · pesquisar · listar · editar
├── 2 Agendar ............. entrega ou retirada (+ retirada vinculada à entrega)
├── 3 Programação do dia .. roteiro ordenado por rota (A1, A2, B1...) + exportar TXT
├── 4 Baixa / alterações .. dar baixa · desfazer baixa · reagendar/mudar rota · cancelar
├── 5 Relatórios .......... atrasados · próximos 7 dias · histórico do cliente
│                           · mesas/cadeiras fora do depósito · entregas sem retirada
└── 6 Estoque total ....... quantidade de mesas e cadeiras da empresa
```

## Leiaute dos arquivos

**clientes.csv** — `id;nome;telefone;endereco;bairro`
```
1;Buffet Alegria;16999990000;Rua A, 10;CENTRO
```

**servicos.csv** — `id;cliente_id;tipo;data;horario;rota;mesas;cadeiras;outros;endereco;bairro;status;motorista;obs;vinculo`
```
1;2;E;24/10/2026;08:00;A1;5;40;;Rua B, 5;JARDIM PAULISTA;FEITO;Joao;Festa 15 anos;2
```
- `tipo`: E = entrega, R = retirada
- `rota`: letra = viagem do carro, número = ordem da parada (A1, A2 → volta ao depósito → B1...)
- `status`: PENDENTE, FEITO, CANCELADO
- `vinculo`: id da retirada/entrega par (0 = sem par)

**config.csv** — `chave;valor` (ESTOQUE_MESAS, ESTOQUE_CADEIRAS)

## Requisitos implementados

| ID | Requisito | Origem |
|----|-----------|--------|
| RF01 | Cadastrar cliente (nome, telefone, endereço, bairro) | Áudio 1 |
| RF02 | Pesquisar cliente por parte do nome ou telefone | Apostila (cadastro completo) |
| RF03 | Listar e editar clientes | Apostila |
| RF04 | Agendar entrega/retirada com tipo, dia, horário, bairro, mesas, cadeiras e outros itens | Áudio 1 |
| RF05 | Informar a rota (A1, A2, B1...) e avisar rota repetida no mesmo dia | Áudio 1 + imagem |
| RF06 | Agendar a retirada ligada a uma entrega, com os mesmos itens | Áudio 2 |
| RF07 | Listar a programação do dia agrupada por viagem e ordenada por rota | Áudio 1 |
| RF08 | Exportar o roteiro do dia em TXT para imprimir | Áudio 2 (planilha impressa) |
| RF09 | Dar baixa registrando o motorista | Áudios 1 e 2 (lousa / riscar no papel) |
| RF10 | Reagendar (data, horário, rota) e cancelar | Áudio 2 |
| RF11 | Listar pendências de dias anteriores sem baixa | Áudio 1 ("o que não foi feito") |
| RF12 | Resumo dos próximos 7 dias | Áudio 2 (lousa semanal) |
| RF13 | Mesas e cadeiras fora do depósito | Regra de negócio |
| RF14 | Entregas que ainda não têm retirada agendada | Áudio 1 ("onde precisa ser feito") |
| RNF01 | Console, programação estruturada, arquivos CSV, sem internet | Disciplina |
| RNF02 | Cada operação é gravada no arquivo na hora da confirmação | Apostila M3 |
| RNF03 | Guardar só nome, telefone e endereço do cliente (LGPD) | Apostila M3 |

## Regras de negócio

- Todo agendamento precisa de mesas, cadeiras ou a descrição de outro item.
- A retirada nunca pode ser antes da entrega ligada a ela.
- Bairro `DEPOSITO` = o cliente busca ou devolve no depósito.
- Datas aceitam `24/10`, `24/10/26` ou `24/10/2026`; horários aceitam `8`, `8h30`, `14:00`.

## Fora do escopo (evolução futura — Projeto de Extensão 2)

- Acesso por vários celulares ao mesmo tempo (os dois motoristas vendo e dando baixa na rota).
  Foi o pedido principal do demandante, mas depende de banco de dados e internet,
  conteúdos de semestres seguintes.
- Aplicativo, painel web ou interface gráfica.
- Controle financeiro / pagamento dos eventos.
- Integração com a planilha Excel online atual.

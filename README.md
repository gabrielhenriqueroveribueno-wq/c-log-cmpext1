Controle de Entregas, Retiradas e Estoque — CMPEXT1
Esboço minimamente funcional do projeto de extensão (TADS-EAD IFSP). Demandante: empresa de locação de mesas, cadeiras e itens para festas. Versão 3.1 — arquivo controle_entregas_retiradas_estoque.cpp.

Como compilar e rodar
Ambiente	Comando
Windows sem compilar	dê dois cliques em controle.exe (já vem pronto no pacote)
Windows (MinGW / Dev-C++)	g++ -std=c++11 -o controle controle_entregas_retiradas_estoque.cpp -lws2_32
Dev-C++ pela interface	Projeto → Opções do projeto → Parâmetros → Linker: -lws2_32
Visual Studio	projeto "Aplicativo de Console" C++, colar o código; nada a configurar
Linux / Mac	g++ -std=c++11 -pthread -o controle controle_entregas_retiradas_estoque.cpp
Os arquivos clientes.csv, itens.csv, servicos.csv e painel.html ficam na mesma pasta do programa. Para começar com dados, copie os CSV da pasta dados_exemplo para junto do programa.

Mapa de funcionalidades
Copiar
MENU PRINCIPAL
├── 1 Clientes ............ cadastrar · pesquisar · listar · editar
├── 2 Novo pedido ......... entrega + retirada obrigatória (Enter = sábado / dia seguinte;
│                           endereço e itens são os mesmos), mostra o livre de cada item
│                           e avisa FALTA ou ESTOQUE BAIXO antes de confirmar
├── 3 Programação ........ Enter = fim de semana (sáb + dom, e sexta/segunda se houver);
│                           por viagem: o que carregar no depósito e o que recolher; TXT
├── 4 Baixa / alterações .. dar baixa · desfazer · reagendar · cancelar pedido
│                           · trocar de viagem (A → B) · cliente não recebeu → fim da fila
├── 5 Relatórios .......... atrasados · próximos 7 dias · histórico do cliente · entregas sem retirada
├── 6 Itens e estoque ..... estoque e alertas · cadastrar · editar/desativar · disponibilidade no período
├── 7 Abrir painel ........ gera painel.html e abre no navegador
└── 8 Servidor celulares .. liga/desliga o servidor na rede local para os motoristas
Painel e celulares
Datas: em qualquer data dá para digitar sab, dom, sex, seg ou hoje. sab é o sábado deste fim de semana (de segunda a sábado, o próximo; no domingo, o de ontem).

painel.html: o programa recria o arquivo a cada gravação com o fim de semana inteiro: viagens, quanto carregar em cada uma, o que já foi feito, alertas e estoque. É uma cópia do momento e funciona sem internet, então pode ser mandado pelo WhatsApp para abrir no celular (o "print do roteiro" pedido pelo Leonardo).

Servidor (opção 8): o programa abre um servidor web na rede local, em segundo plano. O menu continua livre. O programa mostra o endereço (ex.: http://192.168.0.15:8080). Nos celulares conectados ao mesmo Wi-Fi, o motorista abre esse endereço, digita o código de acesso (se houver) e o nome uma vez, e toca em ✓ Entregue / Retirado (ou em "Cliente não recebeu → fim da fila"). A baixa é gravada no servicos.csv na hora, aparece no console e a página se atualiza sozinha a cada 30 segundos.

Limitações: o PC precisa ficar ligado e na mesma rede dos celulares; fora do Wi-Fi da empresa não funciona. Na primeira vez, o Windows pergunta sobre o firewall: permita em redes privadas.

Leiaute dos arquivos
itens.csv — id;nome;total;minimo;ativo

Copiar
1;Mesa;40;5;1
clientes.csv — id;nome;telefone;endereco;bairro

servicos.csv — id;cliente_id;tipo;data;horario;rota;itens;outros;endereco;bairro;status;motorista;obs;vinculo

Copiar
3;2;E;12/11/2026;08:30;A2;1:20|2:160|3:20;montagem;Av. Brasil, 900;CENTRO;PENDENTE;;;4
4;2;R;13/11/2026;18:00;C1;1:20|2:160|3:20;montagem;Av. Brasil, 900;CENTRO;PENDENTE;;;3
itens: id_do_item:quantidade separados por |
tipo: E = entrega, R = retirada · status: PENDENTE, FEITO, CANCELADO
vinculo: id da outra parte do pedido (entrega ↔ retirada)
O servicos.csv da versão 1 (com colunas de mesas e cadeiras) é convertido sozinho na primeira abertura.
Regras de negócio
Todo pedido gera uma entrega e uma retirada. A retirada tem data e horário obrigatórios e não pode ser antes da entrega.
Um item fica reservado do dia da entrega até o dia da retirada (inclusive).
Livre no período = total do item − maior quantidade reservada em algum dia do período.
FALTA: o pedido passa do livre; o programa pergunta se registra assim mesmo.
ESTOQUE BAIXO: o que sobra fica abaixo do mínimo cadastrado; o programa avisa.
Cancelar a entrega ou a retirada cancela o pedido inteiro e libera os itens.
Cliente que não recebe no horário vai para a última parada da última viagem do dia (ex.: D3 → D4).
Parte do pedido que já teve baixa não pode ser cancelada (desfaça a baixa ou reagende).
Requisitos implementados
ID	Requisito	Origem
RF01–RF03	Cadastrar, pesquisar, listar e editar clientes	Áudio 1 / apostila
RF04	Registrar pedido com cliente, itens, dia, horário, bairro e endereço	Áudio 1
RF05	Rota A1, A2, B1… e aviso de rota repetida no dia	Áudio 1 + imagem
RF06	Retirada obrigatória em todo pedido, com mesmo endereço e itens	Pedido do grupo / Áudio 2
RF07–RF08	Programação do dia por viagem e exportação em TXT	Áudios 1 e 2
RF09–RF11	Baixa com motorista, desfazer, reagendar e cancelar	Áudios 1 e 2
RF12–RF15	Atrasados, próximos 7 dias, histórico do cliente, entregas sem retirada	Áudio 1
RF16	Cadastro de todos os itens de locação com total e mínimo	Pedido do grupo
RF17	Disponibilidade de cada item no período do pedido	Pedido do grupo
RF18	Aviso de falta e de estoque abaixo do mínimo	Pedido do grupo
RF19	Itens na rua (saíram com baixa e não voltaram)	Regra de negócio
RF20	Painel em HTML gerado pelo programa	Pedido do grupo
RF21	Baixa pelo celular na rede local	Áudio 1 (vários celulares)
RF22	Código de acesso opcional para os celulares	Segurança
RF23	Programação do fim de semana (sáb + dom, sexta/segunda se houver)	Reunião 11/11
RF24	Quantidade a carregar e a recolher por viagem	Reunião 11/11
RF25	Trocar cliente de viagem	Reunião 11/11
RF26	Cliente que não recebeu vai para o fim da fila (menu e celular)	Reunião 11/11
RNF01	C++ estruturado, console, CSV, sem banco de dados	Disciplina
RNF02	Cada operação é gravada na hora	Apostila M3
RNF03	Só nome, telefone e endereço do cliente (LGPD)	Apostila M3
RNF04	Compila no Windows, Linux e Mac	Disciplina
Fora do escopo (evolução futura)
Acesso de fora da rede da empresa (precisaria de servidor na internet ou na nuvem).
Aplicativo instalado (APK).
Controle financeiro / pagamento dos eventos.
Integração com a planilha Excel online atual.

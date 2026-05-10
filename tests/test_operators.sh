#!/bin/bash

echo "=================================================="
echo "====  Iniciando Experiência: Operadores Extra ===="
echo "====   Configuração: 2 slots, Política: rr    ===="
echo "=================================================="

if [ ! -f "bin/controller" ] || [ ! -f "bin/runner" ]; then
    echo "Aviso: Binários em 'bin/' não encontrados. Certifica-te que corres 'make' primeiro."
    exit 1
fi

LOG_FILE="tmp/log.txt"
> "$LOG_FILE"

read -p "Por favor, inicie o controller noutro terminal com: ./bin/controller 2 rr e pressione [ENTER] para continuar..."

# Preparar ficheiro para o input (<)
echo "linha 1" > tmp/dummy_in.txt
echo "linha 2" >> tmp/dummy_in.txt
echo "linha 3" >> tmp/dummy_in.txt
> tmp/dummy_out.txt
> tmp/dummy_err.txt

START_TIME=$(date +%s.%N)
pids=""

echo ">>> A executar comando com '<', '|' e '>' (cat < tmp/dummy_in.txt | wc -l > tmp/dummy_out.txt)"
./bin/runner -e 1 "cat < tmp/dummy_in.txt | wc -l > tmp/dummy_out.txt" > /dev/null 2>&1 &
pids="$pids $!"

echo ">>> A executar comando com '2>' (ls dir_que_nao_existe 2> tmp/dummy_err.txt)"
./bin/runner -e 2 "ls dir_que_nao_existe 2> tmp/dummy_err.txt" > /dev/null 2>&1 &
pids="$pids $!"

wait $pids
END_TIME=$(date +%s.%N)

./bin/runner -s > /dev/null 2>&1
echo "Controller desligado."

echo ">>> Verificando Resultados dos Operadores:"
NUM_LINHAS=$(cat tmp/dummy_out.txt | tr -d ' ' | tr -d '\n')
if [ "$NUM_LINHAS" == "3" ]; then
    echo "[OK] Redirecionamento de input '<' e output '>' funcionou (3 linhas processadas)."
else
    echo "[FALHOU] Redirecionamento '<' ou '>' falhou. Saída no ficheiro: '$NUM_LINHAS'"
fi

ERR_SIZE=$(wc -c < tmp/dummy_err.txt)
if [ "$ERR_SIZE" -gt 0 ]; then
    echo "[OK] Redirecionamento de erro '2>' funcionou (Erros guardados no ficheiro)."
else
    echo "[FALHOU] Redirecionamento '2>' falhou (Ficheiro vazio)."
fi

MAKESPAN=$(awk "BEGIN {print $END_TIME - $START_TIME}")
printf "Tempo total: %.3fs\n" "$MAKESPAN"

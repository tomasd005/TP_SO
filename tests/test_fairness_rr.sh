#!/bin/bash

echo "=================================================="
echo "==== Iniciando Experiência: Round-Robin (RR) Fairness ===="
echo "====   Configuração: 1 slots, Política: rr    ===="
echo "=================================================="

if [ ! -f "bin/controller" ] || [ ! -f "bin/runner" ]; then
    echo "Aviso: Binários em 'bin/' não encontrados. Certifica-te que corres 'make' primeiro."
    exit 1
fi

LOG_FILE="tmp/log.txt"
> "$LOG_FILE"

read -p "Por favor, inicie o controller noutro terminal com: ./bin/controller 1 rr e pressione [ENTER] para continuar..."

START_TIME=$(date +%s.%N)
pids=""

for i in {1..5}; do
    ./bin/runner -e 1 "sleep 1" > /dev/null 2>&1 &
    pids="$pids $!"
done

sleep 0.1

for i in {1..5}; do
    if [ "$i" -eq 3 ]; then
        ./bin/runner -e 2 "sleep 1 | wc -c > tmp/out.txt" > /dev/null 2>&1 &
    else
        ./bin/runner -e 2 "sleep 1" > /dev/null 2>&1 &
    fi
    pids="$pids $!"
done

sleep 0.2
echo ">>> Estado atual do escalonamento (Runner -c):"
./bin/runner -c

# Esperar apenas pelos runners
wait $pids

END_TIME=$(date +%s.%N)
MAKESPAN=$(awk "BEGIN {print $END_TIME - $START_TIME}")

./bin/runner -s > /dev/null 2>&1
echo "Controller desligado com sucesso."

if [ ! -s "$LOG_FILE" ]; then
    echo "Nenhum resultado obtido no log."
else
    awk -F' ' '
    {
        split($1, uarr, "="); user = uarr[2];
        split($3, darr, "="); dur_str = darr[2];
        gsub(/s$/, "", dur_str); dur = dur_str + 0.0;
        
        count[user]++;
        total[user] += dur;
    }
    END {
        printf "%-10s | %-20s | %-15s\n", "User ID", "Média Duração (s)", "Total Comandos"
        print "--------------------------------------------------"
        for (u in count) {
            printf "%-10s | %-20.3f | %-15d\n", u, total[u]/count[u], count[u]
        }
    }' "$LOG_FILE" | sort -n
fi

printf "Tempo total: %.3fs\n" "$MAKESPAN"

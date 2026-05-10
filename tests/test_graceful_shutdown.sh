#!/bin/bash

echo "=================================================="
echo "==== Iniciando Experiência: Graceful Shutdown ===="
echo "====   Configuração: 2 slots, Política: rr    ===="
echo "=================================================="

if [ ! -f "bin/controller" ] || [ ! -f "bin/runner" ]; then
    echo "Aviso: Binários em 'bin/' não encontrados. Certifica-te que corres 'make' primeiro."
    exit 1
fi

LOG_FILE="tmp/log.txt"
> "$LOG_FILE"

read -p "Por favor, inicie o controller noutro terminal com: ./bin/controller 2 rr e pressione [ENTER] para continuar..."

START_TIME=$(date +%s.%N)
pids=""

echo ">>> A enviar 2 tarefas de longa duração (sleep 3s)..."
./bin/runner -e 1 "sleep 3" > /dev/null 2>&1 &
pids="$pids $!"
./bin/runner -e 2 "sleep 3" > /dev/null 2>&1 &
pids="$pids $!"

# Dar um instante para os runners submeterem o comando ao FIFO
sleep 0.5

echo ">>> A enviar sinal de encerramento (-s)..."
echo ">>> O script deverá bloquear aqui até que os 'sleep 3' concluam no controller!"
./bin/runner -s

# Esperar pelos runners apenas por segurança (eles já deverão ter morrido antes do -s retornar)
wait $pids

END_TIME=$(date +%s.%N)
MAKESPAN=$(awk "BEGIN {print $END_TIME - $START_TIME}")

echo "Controller desligado."

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

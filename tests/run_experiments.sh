#!/bin/bash

echo "=================================================="
echo "======     A Executar todos os testes       ======"
echo "=================================================="

echo ""
echo ">>> 1. TESTE DE JUSTIÇA (FIFO vs RR) <<<"
./tests/test_fairness_fifo.sh
./tests/test_fairness_rr.sh

echo ""
echo ">>> 2. TESTE DE PARALELISMO <<<"
./tests/test_parallelism_1.sh
./tests/test_parallelism_2.sh
./tests/test_parallelism_4.sh

echo ""
echo ">>> 3. TESTES GRACEFUL SHUTDOWN E OPERADORES <<<"
./tests/test_graceful_shutdown.sh
./tests/test_operators.sh

echo ""
echo "Todos os testes foram executados com sucesso!"

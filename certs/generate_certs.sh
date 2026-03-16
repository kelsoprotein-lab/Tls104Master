#!/bin/bash
# TLS证书生成脚本
# 用于生成IEC 104主站测试证书

CERTS_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "====================================="
echo "  IEC 104 TLS证书生成工具"
echo "====================================="
echo ""

# 检查openssl
if ! command -v openssl &> /dev/null; then
    echo "错误: 未找到openssl命令"
    exit 1
fi

cd "$CERTS_DIR"

# 生成CA私钥
echo "1. 生成CA私钥..."
openssl genrsa -out ca.key 2048
if [ $? -ne 0 ]; then
    echo "CA私钥生成失败"
    exit 1
fi

# 生成CA证书
echo "2. 生成CA证书..."
openssl req -new -x509 -days 365 -key ca.key -out ca.crt \
    -subj "/C=CN/ST=Beijing/L=Beijing/O=IEC104Demo/OU=CA/CN=IEC104DemoCA"
if [ $? -ne 0 ]; then
    echo "CA证书生成失败"
    exit 1
fi

# 生成服务器私钥
echo "3. 生成服务器私钥..."
openssl genrsa -out server.key 2048
if [ $? -ne 0 ]; then
    echo "服务器私钥生成失败"
    exit 1
fi

# 生成服务器证书签名请求(CSR)
echo "4. 生成服务器CSR..."
openssl req -new -key server.key -out server.csr \
    -subj "/C=CN/ST=Beijing/L=Beijing/O=IEC104Demo/OU=Server/CN=localhost"
if [ $? -ne 0 ]; then
    echo "服务器CSR生成失败"
    exit 1
fi

# 使用CA签发服务器证书
echo "5. 签发服务器证书..."
openssl x509 -req -days 365 -in server.csr -CA ca.crt -CAkey ca.key \
    -CAcreateserial -out server.crt
if [ $? -ne 0 ]; then
    echo "服务器证书签发失败"
    exit 1
fi

# 生成客户端私钥
echo "6. 生成客户端私钥..."
openssl genrsa -out client.key 2048
if [ $? -ne 0 ]; then
    echo "客户端私钥生成失败"
    exit 1
fi

# 生成客户端证书签名请求(CSR)
echo "7. 生成客户端CSR..."
openssl req -new -key client.key -out client.csr \
    -subj "/C=CN/ST=Beijing/L=Beijing/O=IEC104Demo/OU=Client/CN=IEC104Client"
if [ $? -ne 0 ]; then
    echo "客户端CSR生成失败"
    exit 1
fi

# 使用CA签发客户端证书
echo "8. 签发客户端证书..."
openssl x509 -req -days 365 -in client.csr -CA ca.crt -CAkey ca.key \
    -CAcreateserial -out client.crt
if [ $? -ne 0 ]; then
    echo "客户端证书签发失败"
    exit 1
fi

# 清理临时文件
echo "9. 清理临时文件..."
rm -f server.csr client.csr ca.srl

echo ""
echo "====================================="
echo "  证书生成完成！"
echo "====================================="
echo ""
echo "生成的文件:"
echo "  CA证书:     ca.crt"
echo "  CA私钥:     ca.key (请妥善保管)"
echo "  服务器证书: server.crt"
echo "  服务器私钥: server.key"
echo "  客户端证书: client.crt"
echo "  客户端私钥: client.key"
echo ""
echo "主站使用示例:"
echo "  ./tls104_master -h <ip> -p 2404 -c ca.crt -C client.crt -k client.key"

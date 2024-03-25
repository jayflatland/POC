import bitcoin
# import hashlib

# Generate a random private key
#private_key = bitcoin.random_key()
private_key = "bbf24a103e78d521ff097b22c05086033d0f663d134b4a30fb38cc80d32c23c4"
# Public Key: 04078f5a49b5c4fe846f98a627fa4ede6702666e2ea8f49a4d794ac1c07275c02256e7b2a73c76a98adbb8c9dba5c9b8c76a5186ef67b8b0bc570bd14969038578
# Bitcoin Address: 14yFRRLnSmxJuW8PAXyhuAri7PJ6edZFiU

private_key = "1FAD0768778CBA8319F2B05B10125F4C81E0F2F95A2CBA13E24F482E65178FFB"

# Derive the corresponding public key
public_key = bitcoin.privtopub(private_key)

# Generate Bitcoin address from the public key
bitcoin_address = bitcoin.pubtoaddr(public_key)

print("Private Key:", private_key)
print("Public Key:", public_key)
print("Bitcoin Address:", bitcoin_address)
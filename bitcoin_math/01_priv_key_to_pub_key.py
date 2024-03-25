#!/usr/bin/env python

# Elliptic curve parameters for secp256k1
p  = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F  # Prime modulus
a  = 0x0000000000000000000000000000000000000000000000000000000000000000  # Coefficient 'a' of the curve equation
b  = 0x0000000000000000000000000000000000000000000000000000000000000007  # Coefficient 'b' of the curve equation
Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798  # x-coordinate of base point G
Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8  # y-coordinate of base point G
n  = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141  # Order of the base point

# Generate a random private key
# private_key = 0x1FAD0768778CBA8319F2B05B10125F4C81E0F2F95A2CBA13E24F482E65178FFB
#Public Key: 0xc0c064669faf4758881331d6526993380f93e31b71401d49a836f648d8cb8a96 0x19542b62302491bb8a0ef44079a04ffefd9fecf70893c7794745aae6f98df44f

# REFERENCE
private_key = 0xbbf24a103e78d521ff097b22c05086033d0f663d134b4a30fb38cc80d32c23c4

#FROM bitcoin library
# Public Key: 04078f5a49b5c4fe846f98a627fa4ede6702666e2ea8f49a4d794ac1c07275c02256e7b2a73c76a98adbb8c9dba5c9b8c76a5186ef67b8b0bc570bd14969038578
# Bitcoin Address: 14yFRRLnSmxJuW8PAXyhuAri7PJ6edZFiU

#FROM this code
# Public Key: 0x078f5a49b5c4fe846f98a627fa4ede6702666e2ea8f49a4d794ac1c07275c022 0x56e7b2a73c76a98adbb8c9dba5c9b8c76a5186ef67b8b0bc570bd14969038578
exp_pubkey = 0x078f5a49b5c4fe846f98a627fa4ede6702666e2ea8f49a4d794ac1c07275c022, 0x56e7b2a73c76a98adbb8c9dba5c9b8c76a5186ef67b8b0bc570bd14969038578

#COOL, they match.  Concat the 2 values and prefix with 0x04 and it matches


def p_add(P, Q):
    x1, y1 = P
    x2, y2 = Q
    dx = x2 - x1
    dy = y2 - y1
    # dy * dx ^ (p-2)%p
    m = dy * pow(dx, p - 2, p) % p
    x3 = (m * m - x1 - x2) % p
    y3 = (m * (x1 - x3) - y1) % p
    return x3, y3

def p_double(P):
    x, y = P
    # (3 * x^2 + a) * ( (2*y) ^ (p-2) % p ) % p
    m = (3 * x * x + a) * pow(2 * y, p - 2, p) % p
    x3 = (m * m - x - x) % p
    y3 = (m * (x - x3) - y) % p
    return x3, y3

def scalar_multiply(k, P):
    R = None
    for i in range(256):
        if (k >> i) & 1:
            if R is None:
                R = P
            else:
                R = p_add(R, P)
        P = p_double(P)
    return R

# Compute the corresponding public key
public_key = scalar_multiply(private_key, (Gx, Gy))

print("Private Key:", hex(private_key))
print("Public Key:", hex(public_key[0]), hex(public_key[1]))
assert public_key == exp_pubkey
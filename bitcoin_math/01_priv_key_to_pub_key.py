#!/usr/bin/env python

# Elliptic curve parameters for secp256k1
p  = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F  # Prime modulus
a  = 0x0000000000000000000000000000000000000000000000000000000000000000  # Coefficient 'a' of the curve equation
b  = 0x0000000000000000000000000000000000000000000000000000000000000007  # Coefficient 'b' of the curve equation
Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798  # x-coordinate of base point G
Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8  # y-coordinate of base point G
n  = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141  # Order of the base point

Gx = 55066263022277343669578718895168534326250603453777594175500187360389116729240
Gy = 32670510020758816978083085130507043184471273380659243275938904335757337482424

# Function to perform point addition on the elliptic curve
def point_add(P, Q):
    if P is None:
        return Q
    if Q is None:
        return P
    x1, y1 = P
    x2, y2 = Q
    if x1 == x2 and y1 == y2:
        # Point doubling
        slope = (3 * x1 * x1 + a) * pow(2 * y1, p - 2, p) % p
    else:
        # Point addition
        slope = (y2 - y1) * pow(x2 - x1, p - 2, p) % p
    x3 = (slope * slope - x1 - x2) % p
    y3 = (slope * (x1 - x3) - y1) % p
    return x3, y3

# Function to perform scalar multiplication on the elliptic curve
def scalar_multiply(k, P):
    R = None
    for i in range(256):
        if (k >> i) & 1:
            R = point_add(R, P)
        P = point_add(P, P)
    return R

# Generate a random private key
private_key = 0x1FAD0768778CBA8319F2B05B10125F4C81E0F2F95A2CBA13E24F482E65178FFB

# Compute the corresponding public key
public_key = scalar_multiply(private_key, (Gx, Gy))

print("Private Key:", hex(private_key))
print("Public Key:", hex(public_key[0]), hex(public_key[1]))

if 0:
    import secp256k1

    #y^2=x^3+7

    # Generate a random private key
    private_key = secp256k1.PrivateKey(bytes.fromhex("1FAD0768778CBA8319F2B05B10125F4C81E0F2F95A2CBA13E24F482E65178FFB"))

    # Derive the corresponding public key
    public_key = private_key.pubkey.serialize()

    print("Private Key:", private_key.serialize())
    print("Public Key:", public_key.hex())
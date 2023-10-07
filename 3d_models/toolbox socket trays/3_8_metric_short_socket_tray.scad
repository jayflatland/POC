pad_w = 222.3;
pad_d = 29.9+2;
pad_h = 3;
//cube([pad_w, pad_d, pad_h]);
linear_extrude(height=pad_h)
polygon(points=[
    [0,0],
    [0, pad_d],
    [16, pad_d],
    [pad_w, 18],
    [pad_w, 0]]);

module peg(x, y, d)
{
    w = 9;
    h = 11.8;
    translate([x - w/2, y - w/2, 0])
    {
        cube([w, w, h]);
    }
    translate([x, y, 0])
    cylinder(3.3, d/2, d/2);//just for checking
}
d22 = 29.9;
d19 = 25.8;
d17 = 23.9;
d16 = 21.8;
d15 = 21.8;
d14 = 19.8;
d13 = 17.8;
d12 = 16.8;
d11 = 16.8;
d10 = 16.9;

row1 = [d22, d19, d17, d16, d15, d14, d13, d12, d11, d10];
interspace = 1.0;
x_vals = [
    for (
        i = 0, accum = 0.0;
        i < len(row1);
        accum = accum + row1[i] + interspace, i = i + 1
    )
    accum
];

for ( i = [ 0 : len(row1) - 1] )
{
    d = row1[i];
    x = interspace + x_vals[i] + d / 2;
    y = d/2 + interspace;
    //y = pad_d / 2;
    peg(x, y, d);
    echo(x + d / 2 + interspace);
}


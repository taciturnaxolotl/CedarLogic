# CedarLogic

CedarLogic is a digital logic simulator made for university classroom instruction. It includes all the basic gates, buses, JK and D flip flops, muxes, decoders, and a [Z80 micro-processor](https://en.wikipedia.org/wiki/Zilog_Z80). At [Cedarville University](https://www.cedarville.edu/) it has been used by Computer Architecture 1 students to build and simulate a full [mano-machine](https://en.wikipedia.org/wiki/Mano_machine).

## Contributing

All improvements, especially to stability, are welcome. Please see [Contributing](./docs/Contributing.md) for more.

## Building

To build the source code yourself, [clone the repo](https://docs.github.com/en/repositories/creating-and-managing-repositories/cloning-a-repository)
and read the [Building](./docs/Building.md) instructions.

Building doc also includes a few notes for development.

If you have [Task](https://taskfile.dev) (it is in the Nix devshell), `task`
lists the common build, test, and packaging commands. Cutting a release is
`task release`; see [Releasing](./docs/Releasing.md).

<p align="center">
    <img src="https://raw.githubusercontent.com/taciturnaxolotl/carriage/main/.github/images/line-break.svg" />
</p>

<p align="center">
    <i><code>&copy; 2006-present <a href="https://www.cedarville.edu/">Cedarville University</a></code></i>
</p>

<p align="center">
    <a href="https://github.com/taciturnaxolotl/CedarLogic/blob/master/LICENSE.md"><img src="https://img.shields.io/static/v1.svg?style=for-the-badge&label=License&message=GPL-3.0&logoColor=d9e0ee&colorA=363a4f&colorB=b7bdf8"/></a>
</p>

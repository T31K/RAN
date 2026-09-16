

CREATE PROCEDURE [dbo].[InsertPartyMatch]
	@nSGNum  int,
	@nSvrNum int,
    @nWin    int,
	@nLost   int
AS
	SET NOCOUNT ON

	INSERT INTO LogPartyMatch (SGNum, SvrNum, Win, Lost) 
	VALUES (@nSGNum, @nSvrNum, @nWin, @nLost)

	SET NOCOUNT OFF



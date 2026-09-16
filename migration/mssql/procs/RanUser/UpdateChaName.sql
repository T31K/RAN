
CREATE PROCEDURE [dbo].[UpdateChaName]
	@nUserNum int,
	@szChaName varchar (33)
AS
	SET NOCOUNT ON
	
	UPDATE UserInfo
	SET ChaName=@szChaName
	WHERE UserNum=@nUserNum
	
	SET NOCOUNT OFF

